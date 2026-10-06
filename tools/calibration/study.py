"""Known-intrinsics mount recovery: synthetic XYZ/UV observations, real OpenCV fits."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np

from simulator import project

METHODS = ('iterative', 'epnp', 'sqpnp', 'ransac_epnp_lm')


def observations(camera, rng, count=120):
    """Uniform vehicle-space candidates, filtered by the true camera's valid FOV.

    No detector or scene visibility: this experiment isolates the pose solver.
    Ground and raised points intentionally avoid planar degeneracy.
    """
    points = rng.uniform([-14, -14, 0], [14, 14, 3], size=(50000, 3))
    uv = project(camera, points)
    T = np.asarray(camera['T_camera_from_vehicle'])
    optical = points @ T[:3, :3].T + T[:3, 3]
    theta = np.arctan2(np.linalg.norm(optical[:, :2], axis=1), optical[:, 2])
    r = camera['resolution']
    valid = ((optical[:, 2] > .1) & (theta < 1.35)
             & (uv[:, 0] > 50) & (uv[:, 0] < r['width']-51)
             & (uv[:, 1] > 50) & (uv[:, 1] < r['height']-51))
    points, uv = points[valid][:count], uv[valid][:count]
    if len(points) != count:
        raise ValueError('insufficient valid metric control points')
    return points, uv


def errors(estimate, truth, points, pixels):
    uv = project(estimate, points)
    residual = np.linalg.norm(uv-pixels, axis=1)
    pose = np.asarray(estimate['T_camera_from_vehicle'])
    optical = points @ pose[:3, :3].T + pose[:3, 3]
    theta = np.arctan2(np.linalg.norm(optical[:, :2], axis=1), optical[:, 2])
    size = estimate['resolution']
    invalid = ((optical[:, 2] <= estimate['projection']['z_epsilon_m'])
               | (theta > estimate['projection']['theta_max_rad'])
               | (uv[:, 0] < 0) | (uv[:, 0] > size['width']-1)
               | (uv[:, 1] < 0) | (uv[:, 1] > size['height']-1))
    E, T = (np.asarray(c['T_camera_from_vehicle']) for c in (estimate, truth))
    rotation_error = np.rad2deg(np.arccos(np.clip((np.trace(E[:3, :3] @ T[:3, :3].T)-1)/2, -1, 1)))
    center_e, center_t = (-m[:3, :3].T @ m[:3, 3] for m in (E, T))
    return dict(invalid_validation_count=int(invalid.sum()), validation_rmse_px=float(np.sqrt(np.mean(residual**2))),
                validation_p95_px=float(np.quantile(residual, .95)),
                rotation_error_deg=float(rotation_error),
                center_error_m=float(np.linalg.norm(center_e-center_t)))


def study(dataset, output, build, trials=5, render=False):
    from blender.scenario import perturb, validate
    dataset, output, build = (Path(p).resolve() for p in (dataset, output, build))
    truth_record = json.loads((dataset/'ground_truth.json').read_text())
    manifest = json.loads((dataset/'manifest.json').read_text())
    for name, digest in manifest['sha256'].items():
        path = (dataset/name).resolve()
        if not path.is_relative_to(dataset) or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('dataset image checksum/path mismatch')
    recipe = validate(truth_record['scenario_recipe'])
    nominal = json.loads((dataset/'nominal-config.json').read_text())
    if not 1 <= trials <= 50:
        raise ValueError('trials must be 1..50')
    binary = build/'sv-calibrate'
    output.mkdir(parents=True, exist_ok=False)
    rows, illustrations = [], {}
    conditions = [(sigma, outliers) for sigma in (0., .5, 1.) for outliers in (0., .1)]
    for trial in range(trials):
        trial_recipe = copy.deepcopy(recipe)
        trial_recipe['seed'] += trial
        truth, mounts = perturb(nominal, trial_recipe)
        rng = np.random.default_rng(1000+trial)
        train, validation = [], []
        for cam in truth['cameras']:
            train.append(observations(cam, rng))
            validation.append(observations(cam, rng))
        for sigma, outlier_fraction in conditions:
            condition = f'trial{trial}-sigma{sigma}-outliers{outlier_fraction}'
            folder = output/condition
            folder.mkdir()
            initial = folder/'initial.json'
            initial.write_text(json.dumps(nominal, indent=2)+'\n')
            records = []
            for cam, (points, uv) in zip(truth['cameras'], train):
                noisy = uv+rng.normal(0, sigma, uv.shape)
                n = round(len(uv)*outlier_fraction)
                indices = rng.choice(len(uv), n, replace=False)
                noisy[indices] += rng.uniform(-35, 35, (n, 2))
                records.append(dict(id=cam['id'], points=points.tolist(), pixels=noisy.tolist()))
            inputs = folder/'observations.json'
            inputs.write_text(json.dumps(dict(schema_version=1, cameras=records), indent=2)+'\n')
            for method in METHODS:
                destination = folder/method
                process = subprocess.run([str(binary), 'extrinsics', '--config', str(initial),
                    '--observations', str(inputs), '--output', str(destination), '--method', method],
                    text=True, capture_output=True, timeout=30)
                record = dict(trial=trial, mount_seed=trial_recipe['seed'], sigma_px=sigma,
                              outlier_fraction=outlier_fraction, method=method, mounts=mounts,
                              before_cameras=[errors(e, c, *data) for e, c, data in zip(nominal['cameras'], truth['cameras'], validation)],
                              success=process.returncode == 0, observations_sha256=hashlib.sha256(inputs.read_bytes()).hexdigest())
                if process.returncode:
                    record['error'] = process.stderr.strip()
                else:
                    estimated = json.loads((destination/'config.json').read_text())
                    fit = json.loads((destination/'report.json').read_text())
                    record['cameras'] = [dict(camera_id=c['id'], **errors(e, c, *data),
                                             inliers=info['inliers'], fit_ms=info['fit_ms'])
                                        for e, c, data, info in zip(estimated['cameras'], truth['cameras'],
                                                                   validation, fit['cameras'])]
                    if trial == 0 and sigma == .5 and outlier_fraction == .1:
                        illustrations[method] = estimated
                rows.append(record)
    summary = []
    for sigma, fraction in conditions:
        for method in METHODS:
            matching = [r for r in rows if r['sigma_px'] == sigma and r['outlier_fraction'] == fraction
                        and r['method'] == method]
            cameras = [c for row in matching if row['success'] for c in row['cameras']]
            values = dict(sigma_px=sigma, outlier_fraction=fraction, method=method,
                          successful_trials=sum(r['success'] for r in matching), trials=trials)
            for metric in ('validation_rmse_px', 'rotation_error_deg', 'center_error_m', 'fit_ms'):
                values[metric+'_median'] = float(np.median([c[metric] for c in cameras])) if cameras else None
            values['nominal_validation_rmse_px_median'] = float(np.median([
                c['validation_rmse_px'] for row in matching for c in row['before_cameras']]))
            values['invalid_validation_points'] = sum(c['invalid_validation_count'] for c in cameras)
            summary.append(values)
    if render:
        manifest = json.loads((dataset/'manifest.json').read_text())
        for label, cfg in dict(nominal=nominal, truth=truth_record['true_config'], **illustrations).items():
            case = output/'images'/label
            case.mkdir(parents=True)
            config_path, manifest_path = case/'config.json', case/'manifest.json'
            config_path.write_text(json.dumps(cfg, indent=2)+'\n')
            recording = copy.deepcopy(manifest)
            recording['calibration_ids'] = [c['calibration_id'] for c in cfg['cameras']]
            # sv-bench resolves paths relative to this new manifest.
            recording['frames'] = [dict(row, paths=[str(dataset/p) for p in row['paths']]) for row in manifest['frames']]
            manifest_path.write_text(json.dumps(recording, indent=2)+'\n')
            subprocess.run([str(build/'sv-bench'), '--config', str(config_path), '--manifest', str(manifest_path),
                            '--output', str(case/'render'), '--iterations', '1', '--warmup', '0',
                            '--egl-platform', 'surfaceless'], check=True, capture_output=True, timeout=60)
    report = dict(schema_version=1, suite_id='surround-view-mount-calibration-v1', trials=trials,
                  observations_per_camera_train=120, observations_per_camera_validation=120,
                  nominal_sha256=hashlib.sha256((dataset/'nominal-config.json').read_bytes()).hexdigest(),
                  input_manifest_sha256=hashlib.sha256((dataset/'manifest.json').read_bytes()).hexdigest(),
                  implementation_sha256={str(p.relative_to(Path(__file__).resolve().parents[2])): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in (Path(__file__).resolve(), Path(__file__).resolve().parents[1]/'blender'/'scenario.py',
                                Path(__file__).resolve().parents[1]/'simulator.py')},
                  calibration_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  scenario_recipe=recipe, summary=summary, rows=rows,
                  limitations=['known exact intrinsics; noncoplanar XYZ/UV supplied, no image detector',
                               'UV synthesized from truth; visibility/occlusion not checked',
                               'validation UV is clean; train noise/outliers controlled',
                               'illustrations are actual Blender input/GLES output; not proof of image-based calibration',
                               'RANSAC threshold and LM objective use normalized pinhole coordinates',
                               'not all calibration families; no server calibration job API yet'])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    lines = ['# Сравнение восстановления крепления камер', '',
             'Известные intrinsics, synthetic noncoplanar XYZ/UV. Медианы по успешным trials × 4 камеры.', '',
             '| Noise σ px | Outliers | Method | Success trials | Held-out RMSE px | Rotation deg | Center m |',
             '|---:|---:|---|---:|---:|---:|---:|']
    for row in summary:
        fmt = lambda key: f'{row[key]:.6f}' if row[key] is not None else 'N/A'
        lines.append(f'| {row["sigma_px"]} | {row["outlier_fraction"]} | {row["method"]} | '
                     f'{row["successful_trials"]}/{trials} | {fmt("validation_rmse_px_median")} | '
                     f'{fmt("rotation_error_deg_median")} | {fmt("center_error_m_median")} |')
    lines += ['', '## Первичный машинный отчёт', '', '```json', json.dumps(report, indent=2), '```', '']
    (output/'REPORT.md').write_text('\n'.join(lines))
    return report


def board_image_study(dataset, output, build, training_frames=3):
    """Detect Blender chessboards, fit from early poses, and evaluate a held-out pose."""
    dataset, output, build = (Path(p).resolve() for p in (dataset, output, build))
    if output.exists():
        raise ValueError('output must be a new directory')
    capture = json.loads((dataset/'calibration-board-captures.json').read_text())
    manifest = json.loads((dataset/'manifest.json').read_text())
    truth_record = json.loads((dataset/'ground_truth.json').read_text())
    truth = json.loads((dataset/'config.json').read_text())
    nominal = json.loads((dataset/'nominal-config.json').read_text())
    if not capture.get('cameras') or not capture['cameras'][0].get('views'):
        raise ValueError('capture must contain camera views')
    frames = len(capture['cameras'][0]['views'])
    for record in capture['cameras']:
        if len(record.get('views', [])) != frames:
            raise ValueError('every camera must provide the same number of frame views')
    if training_frames < 2 or training_frames >= frames:
        raise ValueError('at least two train frames and one held-out frame are required')
    for name, digest in manifest['sha256'].items():
        image = (dataset/name).resolve()
        if not image.is_relative_to(dataset) or hashlib.sha256(image.read_bytes()).hexdigest() != digest:
            raise ValueError('dataset image checksum/path mismatch')
    output.mkdir(parents=True)
    for split, choose in (('train', lambda i: i < training_frames),
                          ('validation', lambda i: i >= training_frames)):
        cameras = []
        for record in capture['cameras']:
            views = []
            for index, view in enumerate(record['views']):
                if choose(index):
                    entry = copy.deepcopy(view)
                    entry['image'] = str((dataset/entry['image']).resolve())
                    views.append(entry)
            cameras.append({'id':record['id'], 'views':views})
        (output/f'{split}-input.json').write_text(json.dumps(
            {'schema_version':1, 'board':capture['board'], 'cameras':cameras}, indent=2)+'\n')
    binary = build/'sv-calibrate'
    detect_reports = {}
    observations = {}
    for split in ('train', 'validation'):
        destination = output/f'detected-{split}'
        process = subprocess.run([str(binary), 'board-observations', '--config',
            str(dataset/'config.json'), '--input', str(output/f'{split}-input.json'),
            '--output', str(destination)], capture_output=True, text=True, timeout=120)
        if process.returncode:
            raise RuntimeError(f'{split} board detection failed: {process.stderr.strip()}')
        detect_reports[split] = json.loads((destination/'report.json').read_text())
        observations[split] = json.loads((destination/'observations.json').read_text())
    candidate_dir = output/'candidate'
    process = subprocess.run([str(binary), 'extrinsics', '--config',
        str(dataset/'nominal-config.json'), '--observations',
        str(output/'detected-train'/'observations.json'), '--method', 'ransac_epnp_lm',
        '--output', str(candidate_dir)], capture_output=True, text=True, timeout=120)
    if process.returncode:
        raise RuntimeError(f'extrinsic fit failed: {process.stderr.strip()}')
    candidate = json.loads((candidate_dir/'config.json').read_text())
    fit_report = json.loads((candidate_dir/'report.json').read_text())
    rows = []
    for base, estimated, actual, train, validation, fit in zip(
            nominal['cameras'], candidate['cameras'], truth['cameras'],
            observations['train']['cameras'], observations['validation']['cameras'],
            fit_report['cameras']):
        points = np.asarray(validation['points'], dtype=float)
        pixels = np.asarray(validation['pixels'], dtype=float)
        before = np.linalg.norm(project(base, points)-pixels, axis=1)
        after = np.linalg.norm(project(estimated, points)-pixels, axis=1)
        er, tr = np.asarray(estimated['T_camera_from_vehicle']), np.asarray(actual['T_camera_from_vehicle'])
        center_est, center_true = -er[:3,:3].T@er[:3,3], -tr[:3,:3].T@tr[:3,3]
        angle = np.rad2deg(np.arccos(np.clip((np.trace(er[:3,:3]@tr[:3,:3].T)-1)/2,-1,1)))
        rows.append(dict(camera_id=base['id'], train_points=len(train['points']),
            validation_points=len(validation['points']),
            detected_corners_per_validation_view=detect_reports['validation']['captures'][base['id']]['views'][0]['detected_corners'],
            train_rmse_px=fit['training_rmse_px'],
            before_validation_rmse_px=float(np.sqrt(np.mean(before**2))),
            before_validation_p95_px=float(np.quantile(before,.95)),
            after_validation_rmse_px=float(np.sqrt(np.mean(after**2))),
            after_validation_p95_px=float(np.quantile(after,.95)),
            rotation_error_deg=float(angle),
            center_error_m=float(np.linalg.norm(center_est-center_true)),
            inliers=fit['inliers'], fit_ms=fit['fit_ms']))
    report = dict(schema_version=1, suite_id='blender-image-calibration-v1',
        blender_capture_sha256=truth_record['capture_sha256'],
        input_manifest_sha256=hashlib.sha256((dataset/'manifest.json').read_bytes()).hexdigest(),
        observation_sha256={split:detect_reports[split]['observation_sha256'] for split in detect_reports},
        calibration_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        opencv_version=fit_report['opencv_version'], method=fit_report['method'],
        training_frames=training_frames, validation_frames=frames-training_frames,
        rows=rows, limitations=[
            'Blender rendered chessboard images and measured board poses; no physical camera or target measurement',
            'OpenCV symmetric chessboard corner order is supplied from the known synthetic scene',
            'known intrinsics and known board-to-vehicle transforms; only camera extrinsics are fitted',
            'one held-out pose on one procedural scene; no occlusion or target detection study'])
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    lines = ['# Image-derived extrinsic calibration on Blender frames', '',
        'OpenCV chessboard corners from RGB images; first poses train the native solver, last poses are held out.', '',
        '| Camera | Train corners | Held-out corners | Before RMSE px | After RMSE px | Rotation error ° | Center error m |',
        '|---:|---:|---:|---:|---:|---:|---:|']
    for row in rows:
        lines.append(f"| {row['camera_id']} | {row['train_points']} | {row['validation_points']} | "
            f"{row['before_validation_rmse_px']:.5f} | {row['after_validation_rmse_px']:.5f} | "
            f"{row['rotation_error_deg']:.5f} | {row['center_error_m']:.5f} |")
    lines += ['', '## Limitations', '', *[f'- {item}' for item in report['limitations']],
              '', 'Per-camera reports, corner annotations and candidate config are saved beside this report.', '']
    (output/'REPORT.md').write_text('\n'.join(lines))
    return report


def board_main(arguments):
    parser = argparse.ArgumentParser(description='Image-derived calibration from Blender board captures')
    parser.add_argument('--dataset', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--build', default=Path('build'), type=Path)
    parser.add_argument('--training-frames', type=int, default=3)
    args = parser.parse_args(arguments)
    report = board_image_study(args.dataset, args.output, args.build, args.training_frames)
    print(json.dumps(report['rows'], indent=2))


def main(arguments):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dataset', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--build', default=Path('build'), type=Path)
    parser.add_argument('--trials', default=5, type=int)
    parser.add_argument('--render', action='store_true')
    args = parser.parse_args(arguments)
    report = study(args.dataset, args.output, args.build, args.trials, args.render)
    print(json.dumps(report['summary'], indent=2))
