"""Actual server + native library consumers: listener policy and transport parity."""
import json
import hashlib
import os
from pathlib import Path
import socket
import struct
from PIL import Image
import select
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ipc import Client, pack, receive
from simulator import generate
BUILD = Path(sys.argv[1]).resolve()
CONFIG = Path(sys.argv[2]).resolve()


def ports():
    sockets = [socket.socket() for _ in range(2)]
    for s in sockets:
        s.bind(('127.0.0.1', 0))
    result = [s.getsockname()[1] for s in sockets]
    for s in sockets:
        s.close()
    return result


def ip_socket_inodes(pid):
    result = set()
    for name in ['tcp', 'tcp6', 'udp', 'udp6']:
        for line in Path(f'/proc/{pid}/net/{name}').read_text().splitlines()[1:]:
            result.add(line.split()[9])
    return result


def process_ip_sockets(pid):
    inodes = ip_socket_inodes(pid)
    found = []
    for fd in Path(f'/proc/{pid}/fd').iterdir():
        try:
            link = os.readlink(fd)
        except FileNotFoundError:
            continue
        if link.startswith('socket:[') and link[8:-1] in inodes:
            found.append(link)
    return found


class ClientTransportTests(unittest.TestCase):
    def run_profile(self, profile, qt=False, probe=None, simulator=False, discover=False):
        with tempfile.TemporaryDirectory(prefix='sv-transport-') as td:
            directory = Path(td)
            cfg = json.loads(CONFIG.read_text())
            cfg['output'].update(width=160, height=96)
            fixture = generate(directory / 'fixture', cfg, 12)
            ipc = directory / 'ipc'
            ctl, data = ports()
            connections = {'unix': {'enabled': profile != 'tcp', 'directory': str(ipc)},
                           'tcp': {'enabled': profile != 'unix', 'address': '127.0.0.1',
                                   'control_port': ctl, 'data_port': data},
                           'udp': {'enabled': False}}
            cfg['connections'] = connections
            cfg['telemetry'] = dict(enabled=profile == 'both', period_ms=100, history_capacity=128)
            config = directory / 'config.json'
            config.write_text(json.dumps(cfg))
            server = subprocess.Popen([str(BUILD / 'sv-server'), '--config', str(config),
                '--manifest', str(fixture), '--trace', str(directory / 'trace.jsonl')],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                # Wait on startup trace; connecting a readiness probe would occupy the sole session.
                deadline = time.monotonic() + 8
                while not (directory / 'trace.jsonl').exists():
                    if server.poll() is not None:
                        raise RuntimeError(server.communicate()[1])
                    if time.monotonic() > deadline:
                        raise TimeoutError('server startup')
                    time.sleep(.02)
                if profile == 'unix':
                    self.assertEqual(process_ip_sockets(server.pid), [], 'Unix-only opened an IP socket')
                if profile == 'tcp':
                    self.assertFalse(ipc.exists(), 'TCP-only created Unix endpoints')
                endpoint = ['--unix', str(ipc)] if profile == 'unix' else ['--tcp', '127.0.0.1', str(ctl), str(data)]
                result = subprocess.run([str(probe or BUILD / 'sv-client-probe'), *endpoint, '--frames', '3', '--exercise'],
                    text=True, capture_output=True, timeout=18)
                self.assertEqual(result.returncode, 0, result.stderr)
                messages = [json.loads(line) for line in result.stdout.splitlines()]
                frames = [m for m in messages if m['message_type'] == 11]
                acks = [m for m in messages if m['message_type'] == 21]
                self.assertGreaterEqual(len(frames), 3)
                self.assertTrue(any(not m['accepted'] and m['reason'] == 'unknown_command' for m in acks))
                self.assertTrue(any(m['accepted'] for m in acks))
                self.assertTrue(all(m['pixel_format'] == 'RGBA8' and m['row_origin'] == 'top_left' for m in frames))
                self.assertTrue(all(m['mesh_build_count'] == '1' for m in frames))
                for frame in frames:
                    resources = frame['mesh_resources']
                    self.assertEqual(resources['scope'], 'carrier_position_index_buffers')
                    active = resources['active']
                    self.assertEqual(active['triangles'], frame['mesh_triangles'])
                    self.assertEqual(active['indices'], 3*active['triangles'])
                    self.assertEqual(active['vertex_buffer_bytes'], 12*active['vertices'])
                    self.assertEqual(active['index_buffer_bytes'], 4*active['indices'])
                    self.assertEqual(active['buffer_bytes'], active['vertex_buffer_bytes']+active['index_buffer_bytes'])
                    self.assertEqual(resources['resident'], active)
                # The CLI uses the same installed public library, without Qt or config-file writes.
                config_before = config.read_bytes()
                for arguments, code in ((['state'], 0), (['preset', 'front'], 0),
                                        (['orbit', '.01', '0'], 0), (['zoom', '.1'], 0),
                                        (['command', 'unknown_test_command'], 4)):
                    time.sleep(.08)
                    cli = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                                          *arguments], text=True, capture_output=True, timeout=5)
                    self.assertEqual(cli.returncode, code, cli.stderr)
                    reply = json.loads(cli.stdout)
                    self.assertEqual(reply['accepted'], code == 0)
                # Exercise the shared C++ research runner through the actual client library.
                scenario = directory / 'scenario.json'
                report_path = directory / 'research-report.json'
                variants = [dict(mode=mode) for mode in ['edge_feather', 'hard_best_angle',
                    'angular_feather', 'seam_distance_feather', 'graph_cut_seam', 'multi_band',
                    'graph_cut_multi_band']]
                variants[0]['surface'] = dict(type='cube_floor_v1', half_extent_m=14,
                                             height_m=14, face_cells=8)
                scenario.write_text(json.dumps(dict(schema_version=1, warmup=1, repeats=2,
                                                    seed=17, variants=variants)))
                time.sleep(.08)
                research = subprocess.run([str(BUILD / 'svctl'), *endpoint,
                    'research', str(scenario), str(report_path)],
                    text=True, capture_output=True, timeout=30)
                self.assertEqual(research.returncode, 0, research.stderr + research.stdout)
                report = json.loads(report_path.read_text())
                self.assertTrue(report['success'])
                self.assertTrue(report['restored'])
                self.assertEqual(report['final_state']['experiment_lease']['state'], 'idle')
                self.assertEqual(report['final_state']['paused'], report['initial_state']['paused'])
                self.assertEqual(len(report['scenario_sha256']), 64)
                self.assertEqual(report['trial_order_algorithm'], 'mt19937_fisher_yates_reject_v1')
                plan = report['trial_plan']
                self.assertEqual(len(plan), len(report['samples']))
                digest = hashlib.sha256(json.dumps(plan, separators=(',', ':')).encode()).hexdigest()
                self.assertEqual(report['trial_plan_sha256'], digest)
                for index, sample in enumerate(report['samples']):
                    self.assertEqual(sample['trial_index'], index)
                    for key in ('trial_index', 'frame_index', 'variant', 'block', 'warmup'):
                        self.assertEqual(sample[key], plan[index][key])
                self.assertEqual(len(report['catalog']['source_fingerprint']), 64)
                build = report['catalog']['build_passport']
                self.assertEqual(build['policy'], 'configured_build_v1')
                canonical = json.dumps(build['descriptor'], sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
                self.assertEqual(build['sha256'], hashlib.sha256(b'sv-configured-build-v1\0' + canonical).hexdigest())
                self.assertEqual(build['descriptor']['source']['fingerprint'], report['catalog']['source_fingerprint'])
                self.assertTrue(build['descriptor']['toolchain']['compiler_version'])
                self.assertTrue(build['descriptor']['dependencies']['opencv_configured_version'])
                self.assertEqual(len(report['samples']), 21)
                # Metadata-only runs also reserve the destination before touching the server.
                before_report = report_path.read_bytes()
                refused = subprocess.run([str(BUILD / 'svctl'), *endpoint,
                    'research', str(scenario), str(report_path)],
                    text=True, capture_output=True, timeout=5)
                self.assertNotEqual(refused.returncode, 0)
                self.assertIn('cannot reserve new research report', refused.stderr)
                self.assertEqual(report_path.read_bytes(), before_report)
                self.assertEqual(report['initial_state_origin'], 'experiment_acquire_ack_v1')
                self.assertEqual(report['initial_state']['experiment_lease']['state'], 'active')
                self.assertEqual(report['initial_state']['experiment_lease']['lease_id'], report['lease_id'])
                policy = report['observation_policy']
                self.assertEqual(policy['policy'], 'paused_observation_v1')
                self.assertFalse(policy['capture_frames'])
                self.assertFalse(policy['quality_evaluation'])
                self.assertEqual(policy['capture_consumer'], 'none')
                self.assertTrue(policy['telemetry_stable'])
                self.assertEqual(policy['telemetry_before'], cfg['telemetry'])
                self.assertEqual(policy['telemetry_after'], cfg['telemetry'])
                self.assertEqual(len(report['summaries']), 7)
                for summary in report['summaries']:
                    timing = summary['timings']['render_wall']
                    self.assertEqual(timing['count'], 2)
                    self.assertGreaterEqual(timing['p95_ms'], timing['p50_ms'])
                self.assertEqual(report['baseline_rgba_sha256'], report['restored_rgba_sha256'])
                self.assertEqual(sum(not s['warmup'] for s in report['samples']), 14)
                baseline = report['baseline']
                for sample in report['samples']:
                    self.assertEqual(sample['metadata']['inputs'], baseline['inputs'])
                    self.assertEqual(sample['metadata']['frame_set_id'], baseline['frame_set_id'])
                    self.assertEqual(sample['metadata']['upload_count'], baseline['upload_count'])
                    surface = variants[sample['variant']].get('surface', report['initial_state']['surface'])
                    self.assertEqual(sample['metadata']['surface'], surface)
                for variant in range(7):
                    hashes = {s['rgba_sha256'] for s in report['samples'] if s['variant'] == variant}
                    self.assertEqual(len(hashes), 1, 'identical paused inputs must reproduce RGB')
                self.assertEqual(report['restored_frame']['fusion'], report['initial_state']['fusion'])
                for block in range(3):
                    self.assertEqual({s['variant'] for s in report['samples'] if s['block'] == block},
                                     set(range(7)))
                sequence_path = directory / 'sequence.json'
                sequence_report = directory / 'sequence-report.json'
                sequence_path.write_text(json.dumps(dict(schema_version=1, frames=3,
                    warmup=0, repeats=2, capture_frames=True, reset_replay=True,
                    matrix=dict(base=dict(mode='graph_cut_multi_band', pyramid_levels=3,
                                          seam_solver='alpha_expansion'),
                                axes=dict(pyramid_boundary=['zero','normalized'])))))
                time.sleep(.08)
                sequence = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                    'research', str(sequence_path), str(sequence_report)],
                    text=True, capture_output=True, timeout=20)
                self.assertEqual(sequence.returncode,0,sequence.stderr+sequence.stdout)
                sequence_data = json.loads(sequence_report.read_text())
                self.assertEqual(sequence_data['observation_policy']['capture_consumer'], 'rgba8_files_v1')
                self.assertTrue(sequence_data['observation_policy']['capture_frames'])
                self.assertFalse(sequence_data['observation_policy']['quality_evaluation'])
                self.assertTrue(sequence_data['observation_policy']['telemetry_stable'])
                self.assertEqual(sequence_data['observation_policy']['telemetry_before'], cfg['telemetry'])
                self.assertTrue(sequence_data['restored'])
                self.assertEqual(sequence_data['ready_preparation'], 'reset_first_row')
                self.assertTrue(sequence_data['scenario']['reset_replay'])
                first_inputs = sequence_data['baseline']['inputs']
                self.assertTrue(all(i['source_timestamp_ns'] == '0' for i in first_inputs))
                # Repeating the scenario starts from the same rows/RGB, with distinct
                # source sessions and monotonically increasing delivery identifiers.
                repeated_report = directory / 'sequence-repeat-report.json'
                repeated = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                    'research', str(sequence_path), str(repeated_report)],
                    text=True, capture_output=True, timeout=20)
                self.assertEqual(repeated.returncode, 0, repeated.stderr + repeated.stdout)
                repeated_data = json.loads(repeated_report.read_text())
                self.assertTrue(repeated_data['success'])
                self.assertEqual([s['rgba_sha256'] for s in sequence_data['samples']],
                                 [s['rgba_sha256'] for s in repeated_data['samples']])
                for old, new in zip(sequence_data['frame_baselines'], repeated_data['frame_baselines']):
                    self.assertGreater(int(new['metadata']['frame_set_id']),
                                       int(old['metadata']['frame_set_id']))
                    for a, b in zip(old['metadata']['inputs'], new['metadata']['inputs']):
                        self.assertEqual(a['source_timestamp_ns'], b['source_timestamp_ns'])
                        self.assertNotEqual(a['source_session_id'], b['source_session_id'])
                # The compiled batch service reuses the same runner; every member resets replay.
                batch_scenario = directory / 'batch-scenario.json'
                batch_scenario.write_text(json.dumps(dict(schema_version=1, frames=2,
                    reset_replay=True, capture_frames=True, warmup=0, repeats=1,
                    variants=[dict(mode='multi_band', pyramid_boundary='normalized')])))

                def batch_run(label, scenarios, expected_code):
                    batch_plan = directory / (label + '-plan.json')
                    batch_plan.write_text(json.dumps(dict(schema_version=1, runs=[
                        dict(id=f'case-{index}', scenario=path.name)
                        for index, path in enumerate(scenarios)])))
                    output = directory / label
                    result = subprocess.run([str(BUILD / 'svctl'), *endpoint,
                        '--timeout-ms', '3000', 'research-batch', str(batch_plan), str(output)],
                        text=True, capture_output=True, timeout=30)
                    self.assertEqual(result.returncode, expected_code, result.stderr + result.stdout)
                    return output, json.loads((output / 'batch.json').read_text())

                def portable_batch_run(label, scenarios, expected_code):
                    plan_path = directory / (label + '-plan # %.json')
                    plan_path.write_text(json.dumps(dict(schema_version=1, runs=[
                        dict(id=f'case-{index}', scenario=path.name)
                        for index, path in enumerate(scenarios)])))
                    source_bundle = directory / (label + '-bundle')
                    exported = subprocess.run([str(BUILD / 'svctl'), '--unix', str(directory / 'no-server'),
                        'research-batch-bundle-export', str(plan_path), str(source_bundle)],
                        text=True, capture_output=True, timeout=8)
                    self.assertEqual(exported.returncode, 0, exported.stdout + exported.stderr)
                    receipt = json.loads(exported.stdout)
                    self.assertEqual(receipt['source_plan_sha256'], hashlib.sha256(plan_path.read_bytes()).hexdigest())
                    relocated = directory / (label + ' relocated # %')
                    source_bundle.rename(relocated)
                    verified = subprocess.run([str(BUILD / 'svctl'), '--unix', str(directory / 'no-server'),
                        'research-batch-bundle-verify', str(relocated)], text=True, capture_output=True, timeout=8)
                    self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)
                    self.assertEqual(json.loads(verified.stdout), receipt)
                    output = directory / (label + '-output')
                    result = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                        'research-batch-bundle-run', str(relocated), str(output)],
                        text=True, capture_output=True, timeout=30)
                    self.assertEqual(result.returncode, expected_code, result.stdout + result.stderr)
                    summary = json.loads((output / 'batch.json').read_text())
                    self.assertEqual(summary['input_bundle'], receipt)
                    for row, identity in zip(summary['runs'], receipt['runs']):
                        self.assertEqual(row['id'], identity['id'])
                        report_path = output / row['report']
                        self.assertTrue(report_path.exists())
                        if row['status'] == 'reported':
                            report = json.loads(report_path.read_text())
                            self.assertEqual(report['scenario_input_kind'], 'verified_bundle_snapshot_v1')
                            self.assertEqual(report['input_bundle']['manifest_sha256'], identity['manifest_sha256'])
                        else:
                            self.assertEqual(report_path.stat().st_size, 0)
                    return relocated, summary

                batch_output, batch = batch_run('batch-complete', [batch_scenario] * 2, 0)
                self.assertTrue(batch['success'])
                self.assertTrue(batch['restored'])
                self.assertEqual(batch['completed'], 2)
                self.assertEqual(batch['reported'], 2)
                self.assertEqual(batch['quality_acceptance']['status'], 'not_requested')
                self.assertEqual(batch['plan_sha256'], hashlib.sha256(
                    (directory / 'batch-complete-plan.json').read_bytes()).hexdigest())
                batch_reports = [json.loads((batch_output / row['report']).read_text())
                                 for row in batch['runs']]
                self.assertEqual([s['rgba_sha256'] for s in batch_reports[0]['samples']],
                                 [s['rgba_sha256'] for s in batch_reports[1]['samples']])
                for row, member_report in zip(batch['runs'], batch_reports):
                    self.assertTrue(member_report['restored'])
                    self.assertEqual(member_report['scenario_input_sha256'], row['scenario_input_sha256'])
                    self.assertEqual(row['scenario_input_sha256'], hashlib.sha256(batch_scenario.read_bytes()).hexdigest())
                    self.assertEqual(member_report['ready_preparation'], 'reset_first_row')
                    for sample in member_report['samples']:
                        self.assertTrue((batch_output / sample['rgba_file']).is_file())
                # A server capture is explicitly a regression reference, never independent truth.
                reference_dir = directory / 'quality'
                reference_dir.mkdir()
                width, height = sequence_data['baseline']['width'], sequence_data['baseline']['height']
                mask = bytes(255 if (pixel // 3) % 2 else 0 for pixel in range(width * height * 3))
                (reference_dir / 'mask.ppm').write_bytes(f'P6\n{width} {height}\n255\n'.encode() + mask)
                references = []
                for frame_index in range(3):
                    sample = next(s for s in sequence_data['samples']
                                  if s['frame_index'] == frame_index and s['variant'] == 0)
                    rgba = (directory / sample['rgba_file']).read_bytes()
                    rgb = bytes(rgba[i] for i in range(len(rgba)) if i % 4 != 3)
                    name = f'reference-{frame_index}.ppm'
                    (reference_dir / name).write_bytes(f'P6\n{width} {height}\n255\n'.encode() + rgb)
                    guards = [{key: source[key] for key in
                               ('camera_id', 'calibration_id', 'source_timestamp_ns', 'image_identity', 'camera_model_sha256')}
                              for source in sample['metadata']['inputs']]
                    replay_row = json.loads(fixture.read_text())['frames'][frame_index]
                    for guard, camera, path in zip(guards, cfg['cameras'], replay_row['paths']):
                        image = Image.open(fixture.parent / path).convert('RGB')
                        expected = hashlib.sha256(b'sv-packed-rgb8-v1\0' +
                            struct.pack('>II', *image.size) + image.tobytes()).hexdigest()
                        self.assertEqual(guard['image_identity']['sha256'], expected)
                        projection = camera['projection']
                        values = [projection[key] for key in
                            ('fx', 'fy', 'cx', 'cy', 'theta_max_rad', 'z_epsilon_m')]
                        values += projection['k'] + sum(camera['T_camera_from_vehicle'], [])
                        values = [0.0 if x == 0 else x for x in values]
                        expected_model = hashlib.sha256(b'sv-camera-model-v1\0' +
                            struct.pack('>III', camera['id'], camera['resolution']['width'],
                                        camera['resolution']['height']) + struct.pack('>26d', *values)).hexdigest()
                        self.assertEqual(guard['camera_model_sha256'], expected_model)
                    references.append(dict(rgb=name, mask='mask.ppm', inputs=guards))
                reference = dict(schema_version=2, kind='regression', dataset_id='transport-fixture',
                                 criteria=dict(max_mae_rgb8=0, max_rmse_rgb8=0, min_psnr_db=100),
                                 width=width, height=height, view=sequence_data['baseline']['view'],
                                 frames=references)
                reference_path = reference_dir / 'reference.json'
                reference_path.write_text(json.dumps(reference))
                quality_scenario = directory / 'quality-scenario.json'
                quality_scenario.write_text(json.dumps(dict(schema_version=1, frames=3,
                    reset_replay=True, warmup=1, repeats=2, quality_reference='quality/reference.json',
                    variants=[sequence_data['scenario']['variants'][0]])))
                def gui_quality(expected):
                    test = BUILD / 'sv-simulator-research-tests'
                    if not test.exists():
                        return
                    env = os.environ.copy()
                    env['SV_RESEARCH_GUI_SCENARIO'] = str(quality_scenario)
                    env['SV_RESEARCH_GUI_QUALITY'] = expected
                    result = subprocess.run([str(test), *endpoint], env=env,
                        text=True, capture_output=True, timeout=40)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    qml_test = BUILD / 'sv-simulator-configuration-qml-tests'
                    if expected == 'passed' and qml_test.exists():
                        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
                        env['SV_RESEARCH_GUI_ENDPOINT'] = json.dumps(
                            dict(directory=str(ipc)) if profile == 'unix' else
                            dict(host='127.0.0.1', control_port=ctl, data_port=data))
                        env['SV_RESEARCH_QUALITY_SCREENSHOT'] = '/tmp/sv-reference-result.png'
                        result = subprocess.run([str(qml_test),
                            '--gtest_filter=SimulatorResearchQml.ReferenceWorkflowRendersMeasuredResults:SimulatorResearchBundleQml.VerifiedBundleRunsOnLiveServerThroughTypedControls'],
                            env=env, text=True, capture_output=True, timeout=25)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

                quality_report = directory / 'quality-report.json'
                quality = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                    'research', str(quality_scenario), str(quality_report)],
                    text=True, capture_output=True, timeout=20)
                self.assertEqual(quality.returncode, 0, quality.stderr + quality.stdout)
                quality_data = json.loads(quality_report.read_text())
                self.assertTrue(quality_data['restored'])
                self.assertTrue(quality_data['observation_policy']['quality_evaluation'])
                self.assertEqual(quality_data['observation_policy']['quality_algorithm'], 'masked_rgb8_v1')
                self.assertEqual(quality_data['observation_policy']['telemetry_before'], cfg['telemetry'])
                self.assertEqual(quality_data['observation_policy']['telemetry_after'], cfg['telemetry'])
                self.assertTrue(quality_data['observation_policy']['telemetry_stable'])
                self.assertEqual(quality_data['quality_acceptance']['status'], 'passed')
                self.assertEqual(quality_data['quality_acceptance']['evaluated_frame_variants'], 3)
                self.assertEqual(quality_data['quality_acceptance']['evaluated_samples'], 6)
                self.assertEqual(quality_data['quality_reference']['kind'], 'regression')
                self.assertEqual(quality_data['quality_reference']['manifest_sha256'],
                                 hashlib.sha256(reference_path.read_bytes()).hexdigest())
                self.assertEqual(quality_data['quality_summaries'][0]['frames'], 3)
                self.assertEqual(quality_data['quality_summaries'][0]['selected_pixels'], width * height // 2 * 3)
                self.assertEqual(quality_data['quality_summaries'][0]['pixel_weighted_mae_rgb8'], 0)
                for sample in quality_data['samples']:
                    if sample['warmup']:
                        self.assertNotIn('quality', sample)
                    else:
                        self.assertTrue(sample['quality']['exact_match'])
                        self.assertIsNone(sample['quality']['psnr_db'])
                        self.assertEqual(sample['quality']['selected_pixels'], width * height // 2)
                gui_quality("passed")
                # Portable client-input bundle: export/verify offline, relocate, then run natively.
                bundle = directory / 'bundle'
                exported = subprocess.run([str(BUILD / 'svctl'), '--unix', str(directory / 'no-server'),
                    'research-bundle-export', str(quality_scenario), str(bundle)],
                    text=True, capture_output=True, timeout=8)
                self.assertEqual(exported.returncode, 0, exported.stdout + exported.stderr)
                receipt = json.loads(exported.stdout)
                relocated = directory / 'relocated bundle # %'
                bundle.rename(relocated)
                verified = subprocess.run([str(BUILD / 'svctl'), '--unix', str(directory / 'no-server'),
                    'research-bundle-verify', str(relocated)], text=True, capture_output=True, timeout=8)
                self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)
                self.assertEqual(json.loads(verified.stdout)['manifest_sha256'], receipt['manifest_sha256'])
                bundle_report = directory / 'bundle-report.json'
                completed = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                    'research-bundle-run', str(relocated), str(bundle_report)],
                    text=True, capture_output=True, timeout=20)
                self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
                bundled = json.loads(bundle_report.read_text())
                self.assertTrue(bundled['restored'])
                self.assertEqual(bundled['input_bundle']['manifest_sha256'], receipt['manifest_sha256'])
                self.assertEqual(bundled['scenario_input_kind'], 'verified_bundle_snapshot_v1')
                self.assertEqual(bundled['quality_acceptance']['status'], 'passed')
                self.assertEqual(bundled['quality_summaries'][0]['pixel_weighted_mae_rgb8'], 0)
                manifest = json.loads((relocated / 'bundle.json').read_text())
                asset = next(row['path'] for row in manifest['files'] if row['path'].endswith('-rgb.ppm'))
                with (relocated / asset).open('ab') as changed:
                    changed.write(b'x')
                rejected_output = directory / 'unverified-report.json'
                rejected = subprocess.run([str(BUILD / 'svctl'), '--unix', str(directory / 'no-server'),
                    'research-bundle-run', str(relocated), str(rejected_output)],
                    text=True, capture_output=True, timeout=8)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertFalse(rejected_output.exists(), 'invalid bundle reserved output before preflight')
                batch_bundle, portable = portable_batch_run('portable-success', [quality_scenario] * 2, 0)
                self.assertTrue(portable['restored'])
                self.assertEqual(portable['completed'], 2)
                self.assertEqual(portable['quality_acceptance']['status'], 'passed')
                with (batch_bundle / 'run-1' / 'scenario.json').open('ab') as changed:
                    changed.write(b' ')
                invalid_output = directory / 'portable-late-tamper'
                refused = subprocess.run([str(BUILD / 'svctl'), '--unix', str(directory / 'no-server'),
                    'research-batch-bundle-run', str(batch_bundle), str(invalid_output)],
                    text=True, capture_output=True, timeout=8)
                self.assertEqual(refused.returncode, 3, refused.stdout + refused.stderr)
                self.assertFalse(invalid_output.exists())
                original_reference = reference_path.read_bytes()
                no_criteria = dict(reference)
                del no_criteria['criteria']
                reference_path.write_text(json.dumps(no_criteria))
                try:
                    gui_quality('not_requested')
                finally:
                    reference_path.write_bytes(original_reference)
                # A failed threshold is scientific rejection, while workflow/restore succeeded.
                truth_path = reference_dir / references[0]['rgb']
                original_truth = truth_path.read_bytes()
                altered_truth = bytearray(original_truth)
                altered_truth[-1] ^= 1  # Last pixel is inside alternating ROI.
                truth_path.write_bytes(altered_truth)
                try:
                    rejected_report = directory / 'quality-rejected.json'
                    rejected = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                        'research', str(quality_scenario), str(rejected_report)],
                        text=True, capture_output=True, timeout=20)
                    self.assertEqual(rejected.returncode, 5, rejected.stderr + rejected.stdout)
                    rejected_data = json.loads(rejected_report.read_text())
                    self.assertTrue(rejected_data['success'])
                    self.assertTrue(rejected_data['restored'])
                    self.assertEqual(rejected_data['quality_acceptance']['status'], 'failed')
                    self.assertEqual(rejected_data['quality_acceptance']['failed_frame_variants'], 1)
                    self.assertEqual(len(rejected_data['samples']), 9)
                    self.assertTrue(any(not s['quality']['acceptance']['passed']
                                        for s in rejected_data['samples'] if not s['warmup']))
                    gui_quality('failed')
                    _, portable_rejected = portable_batch_run('portable-quality-rejected',
                        [quality_scenario, batch_scenario], 5)
                    self.assertTrue(portable_rejected['restored'])
                    self.assertEqual(portable_rejected['completed'], 2)
                    self.assertEqual([row['quality_status'] for row in portable_rejected['runs']],
                                     ['failed', 'not_requested'])
                    _, rejected_batch = batch_run('batch-quality-rejected',
                        [quality_scenario, batch_scenario], 5)
                    self.assertTrue(rejected_batch['success'])
                    self.assertTrue(rejected_batch['restored'])
                    self.assertEqual(rejected_batch['completed'], 2)
                    self.assertEqual(rejected_batch['quality_acceptance']['status'], 'failed')
                    self.assertEqual([row['quality_status'] for row in rejected_batch['runs']],
                                     ['failed', 'not_requested'])
                finally:
                    truth_path.write_bytes(original_truth)
                # Change source bytes with identical manifest path, timestamps and calibration IDs.
                first_path = fixture.parent / json.loads(fixture.read_text())['frames'][0]['paths'][0]
                original_image = first_path.read_bytes()
                changed = bytearray(original_image)
                changed[-1] ^= 1
                first_path.write_bytes(changed)
                try:
                    content_report = directory / 'quality-content-mismatch.json'
                    content = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                        'research', str(quality_scenario), str(content_report)],
                        text=True, capture_output=True, timeout=20)
                    self.assertNotEqual(content.returncode, 0)
                    content_data = json.loads(content_report.read_text())
                    self.assertTrue(content_data['restored'])
                    self.assertEqual(content_data['quality_acceptance']['status'], 'incomplete')
                    self.assertEqual(content_data['error'], 'quality reference input guard mismatch')
                    gui_quality('incomplete')
                    _, portable_failed = portable_batch_run('portable-guard-failed',
                        [quality_scenario, batch_scenario], 4)
                    self.assertTrue(portable_failed['restored'])
                    self.assertEqual(portable_failed['reported'], 1)
                    self.assertEqual(portable_failed['completed'], 0)
                    self.assertEqual(portable_failed['runs'][1]['status'], 'not_started')
                    failed_output, failed_batch = batch_run('batch-guard-failed',
                        [quality_scenario, batch_scenario], 4)
                    self.assertFalse(failed_batch['success'])
                    self.assertTrue(failed_batch['restored'])
                    self.assertEqual(failed_batch['completed'], 0)
                    self.assertEqual(failed_batch['reported'], 1)
                    self.assertEqual(failed_batch['quality_acceptance']['status'], 'incomplete')
                    self.assertEqual(failed_batch['runs'][1]['status'], 'not_started')
                    self.assertEqual((failed_output / 'case-1.json').stat().st_size, 0)
                finally:
                    first_path.write_bytes(original_image)
                # A mismatched virtual view is a failed evaluation with lease cleanup.
                reference['view'] = dict(reference['view'], distance_m=reference['view']['distance_m'] + .25)
                reference_path.write_text(json.dumps(reference))
                mismatch_report = directory / 'quality-mismatch-report.json'
                mismatch = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                    'research', str(quality_scenario), str(mismatch_report)],
                    text=True, capture_output=True, timeout=20)
                self.assertNotEqual(mismatch.returncode, 0)
                mismatch_data = json.loads(mismatch_report.read_text())
                self.assertFalse(mismatch_data['success'])
                self.assertTrue(mismatch_data['restored'])
                self.assertIn('guard mismatch', mismatch_data['error'])
                self.assertFalse(any(not s['warmup'] for s in mismatch_data['samples']))
                # Malformed references are refused before report reservation/server mutation.
                reference['kind'] = 'unverified_truth'
                reference_path.write_text(json.dumps(reference))
                invalid_report = directory / 'quality-invalid-report.json'
                invalid = subprocess.run([str(BUILD / 'svctl'), *endpoint,
                    'research', str(quality_scenario), str(invalid_report)],
                    text=True, capture_output=True, timeout=5)
                self.assertNotEqual(invalid.returncode, 0)
                self.assertFalse(invalid_report.exists())
                self.assertFalse(sequence_data['cursor_restored'])
                self.assertEqual(sequence_data['restore_scope'],'fusion_surface_pause_only')
                self.assertIn('alpha_expansion',sequence_data['catalog']['seam_solver'])
                self.assertEqual(sequence_data['scenario']['variants'][1]['seam_solver'],'alpha_expansion')
                self.assertEqual(sequence_data['samples'][0]['metadata']['fusion'],
                    sequence_data['scenario']['variants'][sequence_data['samples'][0]['variant']])
                self.assertEqual(len(sequence_data['frame_baselines']),3)
                self.assertEqual(len(sequence_data['samples']),12)
                self.assertEqual(len({b['metadata']['frame_set_id'] for b in sequence_data['frame_baselines']}),3)
                self.assertEqual(sequence_data['last_baseline_rgba_sha256'],sequence_data['restored_rgba_sha256'])
                for sample in sequence_data['samples']:
                    planned = sequence_data['trial_plan'][sample['trial_index']]
                    for key in ('trial_index', 'frame_index', 'variant', 'block', 'warmup'):
                        self.assertEqual(sample[key], planned[key])
                    baseline = sequence_data['frame_baselines'][sample['frame_index']]['metadata']
                    self.assertEqual(sample['metadata']['inputs'],baseline['inputs'])
                    if sample['metadata']['fusion'].get('seam_solver') == 'alpha_expansion':
                        stats = sample['metadata']['seam_optimization']
                        self.assertLessEqual(stats['final_energy'],stats['initial_energy'])
                        self.assertLessEqual(stats['sweeps'],8)
                        self.assertIsInstance(stats['converged'],bool)
                    else:
                        self.assertNotIn('seam_optimization',sample['metadata'])
                    pixels = (directory/sample['rgba_file']).read_bytes()
                    self.assertEqual(hashlib.sha256(pixels).hexdigest(),sample['rgba_sha256'])
                    self.assertEqual(len(pixels),160*96*4)
                # A repeated invocation must not truncate its previous report/captures.
                before = sequence_report.read_bytes()
                repeated = subprocess.run([str(BUILD / 'svctl'), *endpoint,
                    'research', str(sequence_path), str(sequence_report)],
                    text=True,capture_output=True,timeout=5)
                self.assertNotEqual(repeated.returncode,0)
                self.assertEqual(sequence_report.read_bytes(),before)
                # Native GTest coverage is optional (SV_GTEST_TESTS); CLI checks above always run.
                if (BUILD / 'sv-research-scenario-tests').exists():
                    test_endpoint = ({'directory': str(ipc)} if profile == 'unix' else
                        dict(host='127.0.0.1', control_port=ctl, data_port=data))
                    env = os.environ.copy()
                    env['SV_RESEARCH_TEST_ENDPOINT'] = json.dumps(test_endpoint)
                    time.sleep(.08)
                    interrupted = subprocess.run([str(BUILD / 'sv-research-scenario-tests'),
                        '--gtest_filter=ResearchScenarioIntegration.*'], env=env,
                        text=True, capture_output=True, timeout=10)
                    self.assertEqual(interrupted.returncode, 0, interrupted.stdout + interrupted.stderr)
                self.assertEqual(config.read_bytes(), config_before)
                if qt and (BUILD / 'sv-client').exists():
                    time.sleep(.1)
                    env = os.environ.copy()
                    env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
                    result = subprocess.run([str(BUILD / 'sv-client'), *endpoint, '--smoke'],
                        text=True, capture_output=True, env=env, timeout=8)
                    # Qt positional Unix directory differs from the probe CLI.
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn('ui_receive', result.stdout, result.stderr)
                    self.assertIn('ui_present_submit', result.stdout, result.stderr)
                research_gui = BUILD / 'sv-simulator-research-tests'
                if research_gui.exists():
                    before_gui_research = config.read_bytes()
                    result = subprocess.run([str(research_gui), *endpoint],
                        text=True, capture_output=True, timeout=40)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(config.read_bytes(), before_gui_research,
                                     'GUI research persisted server configuration')
                if simulator and (BUILD / 'examples/sv-simulator/sv-simulator').exists():
                    env = os.environ.copy()
                    env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
                    if discover:
                        env['SV_IPC_DIR'] = str(ipc)
                        simulator_endpoint = []
                    else:
                        simulator_endpoint = endpoint
                    result = subprocess.run(
                        [str(BUILD / 'examples/sv-simulator/sv-simulator'),
                         *simulator_endpoint, '--smoke'],
                        text=True, capture_output=True, env=env, timeout=12)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    runtime_test = BUILD / 'sv-simulator-runtime-tests'
                    if runtime_test.exists():
                        config_before = config.read_bytes()
                        result = subprocess.run([str(runtime_test), *endpoint],
                            text=True, capture_output=True, timeout=25)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertEqual(config.read_bytes(), config_before,
                                         'runtime GUI settings persisted server config')
            finally:
                server.terminate()
                _, err = server.communicate(timeout=5)
                self.assertEqual(server.returncode, 0, err)
            self.assertFalse((ipc / 'control.sock').exists())
            self.assertFalse((ipc / 'data.sock').exists())

    def test_unix_only_no_ip_socket(self):
        self.run_profile('unix', simulator=True, discover=True)

    def test_tcp_only_native_and_qt(self):
        self.run_profile('tcp', qt=True, simulator=True)

    def test_combined_profile(self):
        self.run_profile('both')

    def test_explicit_config_rejects_cli_override_and_udp(self):
        with tempfile.TemporaryDirectory(prefix='sv-reject-') as td:
            directory = Path(td)
            for connections in [{'udp': {'enabled': True}}, {'unix': {'enabled': True, 'directory': str(directory / 'ipc')}}]:
                cfg = json.loads(CONFIG.read_text())
                cfg['connections'] = connections
                config = directory / 'config.json'
                config.write_text(json.dumps(cfg))
                result = subprocess.run([str(BUILD / 'sv-server'), '--config', str(config),
                    '--manifest', 'not-needed.json', '--ipc-dir', str(directory / 'override')],
                    text=True, capture_output=True, timeout=3)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((directory / 'override').exists())
                self.assertTrue('not implemented' in result.stderr or 'cannot override' in result.stderr, result.stderr)

    def test_installed_cmake_consumer(self):
        with tempfile.TemporaryDirectory(prefix='sv-installed-client-') as td:
            directory = Path(td)
            prefix = directory / 'prefix'
            subprocess.run(['cmake', '--install', str(BUILD), '--prefix', str(prefix)], check=True, capture_output=True)
            consumer = directory / 'consumer'
            subprocess.run(['cmake', '-S', str(ROOT / 'tests/fixtures/client-consumer'), '-B', str(consumer),
                f'-DCMAKE_PREFIX_PATH={prefix}'], check=True, capture_output=True)
            subprocess.run(['cmake', '--build', str(consumer), '-j', '2'], check=True, capture_output=True)
            binary = consumer / 'sv-client-probe'
            dependencies = subprocess.check_output(['ldd', str(binary)], text=True)
            for forbidden in ['opencv', 'libQt', 'libEGL', 'libGLES']:
                self.assertNotIn(forbidden, dependencies)
            self.run_profile('tcp', probe=binary)

    def test_library_reconnect_after_server_restart(self):
        with tempfile.TemporaryDirectory(prefix='sv-reconnect-') as td:
            directory = Path(td)
            cfg = json.loads(CONFIG.read_text())
            manifest = generate(directory / 'fixture', cfg, 12)
            ctl, data = ports()
            cfg['connections'] = {'tcp': {'enabled': True, 'address': '127.0.0.1',
                'control_port': ctl, 'data_port': data}}
            config = directory / 'config.json'
            config.write_text(json.dumps(cfg))
            def start():
                return subprocess.Popen([str(BUILD / 'sv-server'), '--config', str(config), '--manifest', str(manifest),
                    '--trace', str(directory / 'trace.jsonl')], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            server = start()
            probe = None
            try:
                deadline = time.monotonic() + 8
                while not (directory / 'trace.jsonl').exists():
                    if server.poll() is not None: raise RuntimeError(server.communicate()[1])
                    if time.monotonic() > deadline: raise TimeoutError('startup')
                    time.sleep(.02)
                probe = subprocess.Popen([str(BUILD / 'sv-client-probe'), '--tcp', '127.0.0.1', str(ctl), str(data),
                    '--frames', '60', '--allow-reconnect'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                messages = []
                # Native probe flushes each line; first actual frame proves a complete session.
                while True:
                    line = probe.stdout.readline()
                    if not line: raise RuntimeError(probe.communicate()[1])
                    message = json.loads(line)
                    messages.append(message)
                    if message.get('message_type') == 11: break
                server.terminate()
                server.communicate(timeout=5)
                server = start()
                out, err = probe.communicate(timeout=15)
                self.assertEqual(probe.returncode, 0, err)
                messages += [json.loads(line) for line in out.splitlines()]
                sessions = {m['session_id'] for m in messages if m.get('message_type') == 11}
                self.assertEqual(len(sessions), 2)
                self.assertGreaterEqual(sum(m.get('client_state') == 'ready' for m in messages), 2)
            finally:
                if probe and probe.poll() is None:
                    probe.kill(); probe.communicate(timeout=3)
                server.terminate(); _, err = server.communicate(timeout=5)
                self.assertEqual(server.returncode, 0, err)

    def test_pause_step_decode_and_irregular_intervals(self):
        with tempfile.TemporaryDirectory(prefix='sv-step-') as td:
            directory = Path(td)
            cfg = json.loads(CONFIG.read_text())
            manifest = generate(directory / 'fixture', cfg, 4)
            recording = json.loads(manifest.read_text())
            times = [0, 200000000, 550000000, 1000000000]
            for row, timestamp in zip(recording['frames'], times):
                row['scenario_timestamp_ns'] = str(timestamp)
            recording['frames'][0]['paths'][0] = None  # Reset must not reuse old camera 0.
            manifest.write_text(json.dumps(recording))
            ipc = directory / 'ipc'
            trace = directory / 'trace.jsonl'
            server = subprocess.Popen([str(BUILD / 'sv-server'), '--config', str(CONFIG), '--manifest', str(manifest),
                '--ipc-dir', str(ipc), '--trace', str(trace), '--loop', 'false'],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            client = None
            try:
                deadline = time.monotonic() + 8
                while not (ipc / 'data.sock').exists():
                    if server.poll() is not None: raise RuntimeError(server.communicate()[1])
                    if time.monotonic() > deadline: raise TimeoutError('startup')
                    time.sleep(.02)
                client = Client(ipc)
                client.frame()
                ack = client.command('pause')
                def revision_frame(ack):
                    while True:
                        h, pixels = client.frame()
                        if int(h['state_revision']) >= int(ack['state_revision']): return h
                first = revision_frame(ack)
                moved = revision_frame(client.command('orbit', azimuth_delta_rad=.2, elevation_delta_rad=0.))
                self.assertEqual(first['upload_count'], moved['upload_count'])
                self.assertEqual(first['decode_count'], moved['decode_count'])
                self.assertEqual(moved['mesh_build_count'], '1')
                stepped = revision_frame(client.command('step'))
                self.assertEqual(int(stepped['frame_set_id']), int(moved['frame_set_id']) + 1)
                self.assertTrue(stepped['paused'])
                self.assertEqual(int(stepped['decode_count']), int(moved['decode_count']) + 4)
                previous = stepped
                for _ in range(2):
                    reset = revision_frame(client.command('replay_reset'))
                    self.assertTrue(reset['paused'])
                    self.assertEqual(reset['health'], 'DEGRADED')
                    self.assertFalse(reset['inputs'][0]['used'])
                    self.assertGreater(int(reset['frame_set_id']), int(previous['frame_set_id']))
                    self.assertEqual(int(reset['decode_count']), int(previous['decode_count']) + 3)
                    for old, new in zip(previous['inputs'], reset['inputs']):
                        if new['used']:
                            self.assertEqual(new['source_timestamp_ns'], '0')
                            self.assertNotEqual(new['source_session_id'], old['source_session_id'])
                    previous = reset
                # Continue from row 1, not from the pre-reset cursor.
                stepped = revision_frame(client.command('step'))
                self.assertTrue(all(i['source_timestamp_ns'] == str(times[1]) for i in stepped['inputs']))
                # 500 commands in bounded bursts; control/video progress independently on pause.
                baseline_revision = int(stepped['state_revision'])
                final = stepped
                for start in range(0, 500, 16):
                    count = min(16, 500 - start)
                    for _ in range(count):
                        client.command_id += 1
                        client.control.sendall(pack(20, dict(command_id=str(client.command_id),
                            type='orbit', azimuth_delta_rad=.001, elevation_delta_rad=0.)))
                    for _ in range(count):
                        kind, ack, _ = receive(client.control)
                        self.assertEqual(kind, 21)
                        self.assertTrue(ack['accepted'])
                    if select.select([client.data], [], [], 0)[0]:
                        final = client.frame()[0]
                if int(final['state_revision']) < int(ack['state_revision']):
                    final = revision_frame(ack)
                self.assertEqual(int(final['state_revision']), baseline_revision + 500)
                self.assertEqual(final['decode_count'], stepped['decode_count'])
                self.assertEqual(final['upload_count'], stepped['upload_count'])
                self.assertEqual(final['mesh_build_count'], stepped['mesh_build_count'])
                client.command('resume')
                time.sleep(1.3)
                client.close()
                client = None
                time.sleep(.08)  # Allow the single-session server to observe disconnect.
                scenario = directory / 'incomplete.json'
                report_path = directory / 'incomplete-report.json'
                scenario.write_text(json.dumps(dict(schema_version=1, reset_replay=True,
                    warmup=0, repeats=1, variants=[dict(mode='edge_feather')])))
                failed = subprocess.run([str(BUILD / 'svctl'), '--unix', str(ipc),
                    'research', str(scenario), str(report_path)],
                    text=True, capture_output=True, timeout=15)
                self.assertNotEqual(failed.returncode, 0, failed.stdout)
                report = json.loads(report_path.read_text())
                self.assertFalse(report['success'])
                self.assertTrue(report['restored'])
                self.assertEqual(report['ready_preparation'], 'reset_first_row')
                self.assertEqual(report['error'], 'research requires a READY replay frame set')
                self.assertEqual(report['samples'], [])

            finally:
                if client: client.close()
                server.terminate()
                _, err = server.communicate(timeout=5)
                self.assertEqual(server.returncode, 0, err)
            events = [json.loads(line) for line in trace.read_text().splitlines()]
            # Frame sets after resume preserve the 350/450 ms scenario intervals.
            selected = [e for e in events if e['event'] == 'frame_set']
            intervals = [(int(b['timestamp_ns']) - int(a['timestamp_ns'])) / 1e6 for a, b in zip(selected, selected[1:])]
            self.assertTrue(any(420 <= interval <= 700 for interval in intervals), intervals)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]], verbosity=2)
