"""Actual server + native library consumers: listener policy and transport parity."""
import json
import hashlib
import os
from pathlib import Path
import socket
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
                self.assertEqual(len(report['catalog']['source_fingerprint']), 64)
                self.assertEqual(len(report['samples']), 21)
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
                    capture_frames=True, warmup=0, repeats=2, seed=31,
                    variants=[dict(mode='graph_cut_multi_band',pyramid_boundary=p,
                        seam_solver='alpha_expansion' if p == 'normalized' else 'binary_pairs')
                        for p in ('zero','normalized')])))
                time.sleep(.08)
                sequence = subprocess.run([str(BUILD / 'svctl'), *endpoint, '--timeout-ms', '3000',
                    'research', str(sequence_path), str(sequence_report)],
                    text=True, capture_output=True, timeout=20)
                self.assertEqual(sequence.returncode,0,sequence.stderr+sequence.stdout)
                sequence_data = json.loads(sequence_report.read_text())
                self.assertTrue(sequence_data['restored'])
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
    unittest.main(argv=[sys.argv[0]])
