"""Recording producer lifecycle against real stream sockets; no GPU is required."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ipc import pack, receive
from producer import ProducerError, stream


def fixture(directory, size=(8, 6)):
    cameras = [dict(id=i, calibration_id=f'fixture-{i}',
        resolution=dict(width=size[0], height=size[1])) for i in range(4)]
    config = dict(cameras=cameras, source=dict(type='socket', cameras=[
        dict(camera_id=i, transport='unix', path=str(directory / f'camera{i}.sock')) for i in range(4)]))
    import hashlib
    paths, hashes = [], {}
    for i in range(4):
        path = directory / f'camera{i}.ppm'
        Image.new('RGB', size, (20+i*30, 80, 150)).save(path)
        paths.append(path.name)
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(schema_version=1, calibration_ids=[c['calibration_id'] for c in cameras], sha256=hashes,
        frames=[dict(scenario_timestamp_ns=str(i*33333333), paths=paths, offset_ns=[0]*4) for i in range(2)])
    manifest_path = directory / 'manifest.json'
    manifest_path.write_text(json.dumps(manifest))
    return config, manifest_path


class CameraReceivers:
    def __init__(self, config, *, drop_first=False, stalled=None):
        self.stop = threading.Event()
        self.handshake = threading.Event()
        self.drop_first, self.stalled = drop_first, stalled
        self.listeners, self.clients, self.workers = [], [], []
        self.frames = [[] for _ in range(4)]
        self.accepted = [0]*4
        self.errors = []
        self.lock = threading.Lock()
        for endpoint in config['source']['cameras']:
            listener = socket.socket(socket.AF_UNIX)
            listener.bind(endpoint['path'])
            listener.listen(2)
            listener.settimeout(.05)
            self.listeners.append(listener)
            worker = threading.Thread(target=self.worker, args=(endpoint['camera_id'], listener))
            worker.start()
            self.workers.append(worker)

    def worker(self, camera, listener):
        try:
            while not self.stop.is_set():
                try:
                    client, _ = listener.accept()
                except socket.timeout:
                    continue
                with self.lock:
                    self.clients.append(client)
                try:
                    client.settimeout(.1)
                    kind, header, _ = receive(client)
                    if kind != 1 or header['camera_id'] != camera:
                        raise ValueError('fixture handshake mismatch')
                    self.accepted[camera] += 1
                    session = f'fixture-{camera}-{self.accepted[camera]}'
                    if camera == 0 and self.stalled == 'handshake':
                        self.handshake.set()
                        self.stop.wait(3)
                        continue
                    if camera == 0 and self.stalled == 'drip':
                        reply = pack(2, dict(session_id=session, camera_id=camera, timestamp_basis='server_delivery'))
                        for byte in reply:
                            client.sendall(bytes([byte]))
                            if self.stop.wait(.02):
                                break
                        continue
                    client.sendall(pack(2, dict(session_id=session, camera_id=camera,
                        timestamp_basis='wrong' if camera == 0 and self.stalled == 'invalid' else 'server_delivery')))
                    if camera == 0 and self.stalled == 'payload':
                        self.handshake.set()
                        self.stop.wait(3)
                        continue
                    while not self.stop.is_set():
                        try:
                            kind, header, pixels = receive(client)
                        except socket.timeout:
                            continue
                        if kind != 10 or len(pixels) != header['stride_bytes'] * header['height']:
                            raise ValueError('fixture frame mismatch')
                        self.frames[camera].append((header['session_id'], int(header['sequence_id'])))
                        if camera == 0 and self.drop_first and self.accepted[camera] == 1:
                            break
                except (OSError, EOFError):
                    pass
                finally:
                    client.close()
                    with self.lock:
                        self.clients.remove(client)
        except OSError as error:
            if not self.stop.is_set():
                self.errors.append(error)
        except Exception as error:
            self.errors.append(error)

    def close(self):
        self.stop.set()
        for listener in self.listeners:
            listener.close()
        with self.lock:
            for client in self.clients:
                client.close()
        for worker in self.workers:
            worker.join(timeout=2)
            if worker.is_alive():
                raise RuntimeError('fixture receiver did not stop')
        if self.errors:
            raise RuntimeError(self.errors)


class ProducerTests(unittest.TestCase):
    def test_reconnect_and_session_sequences(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            directory = Path(temporary)
            config, manifest = fixture(directory)
            receivers = CameraReceivers(config, drop_first=True)
            try:
                path = directory / 'report.json'
                result = stream(config, manifest, loops=12, reconnect_delay_ms=10,
                    timeout_ms=100, report_path=path)
                self.assertEqual(result, json.loads(path.read_text()))
                markdown = path.with_suffix('.md').read_text()
                self.assertEqual(result, json.loads(markdown.split('```json\n')[1].split('\n```')[0]))
                self.assertEqual(result['status'], 'completed')
                first = result['cameras'][0]
                self.assertGreaterEqual(first['reconnects'], 1)
                self.assertGreater(first['transport_failures'], 0)
                self.assertEqual({c['status'] for c in result['cameras']}, {'completed'})
                for camera in result['cameras']:
                    self.assertEqual(camera['scheduled_frames'], camera['sent_frames'] +
                        camera['skipped_late_frames'] + camera['skipped_missing_frames'])
                sessions = {}
                for session, sequence in receivers.frames[0]:
                    sessions.setdefault(session, []).append(sequence)
                self.assertGreaterEqual(len(sessions), 2)
                for sequences in sessions.values():
                    self.assertEqual(sequences[0], 0)
                    self.assertEqual(sequences, sorted(set(sequences)))
                self.assertEqual([c['connections'] for c in result['cameras'][1:]], [1]*3)
            finally:
                receivers.close()

    def test_retry_exhaustion_writes_failure_report(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            directory = Path(temporary)
            config, manifest = fixture(directory)
            path = directory / 'failed.json'
            with self.assertRaises(ProducerError) as raised:
                stream(config, manifest, loops=1000, reconnect_attempts=2,
                    reconnect_delay_ms=10, timeout_ms=50, max_lateness_ms=1000, report_path=path)
            report = json.loads(path.read_text())
            self.assertEqual(report, raised.exception.report)
            self.assertEqual(report['status'], 'failed')
            for camera in report['cameras']:
                self.assertEqual(camera['connect_attempts'], 3)
                self.assertEqual(camera['transport_failures'], 3)
                self.assertEqual(camera['sent_frames'], 0)
                self.assertEqual(camera['status'], 'failed')
            self.assertLess(report['duration_ms'], 1500)

    def test_stop_during_stalled_handshake(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            config, manifest = fixture(Path(temporary))
            receivers = CameraReceivers(config, stalled='handshake')
            stop, results, errors = threading.Event(), [], []
            def run():
                try:
                    results.append(stream(config, manifest, loops=1000, stop=stop, timeout_ms=100))
                except Exception as error:
                    errors.append(error)
            worker = threading.Thread(target=run)
            worker.start()
            try:
                self.assertTrue(receivers.handshake.wait(2))
                before = time.monotonic()
                stop.set()
                worker.join(timeout=1)
                self.assertFalse(worker.is_alive())
                self.assertLess(time.monotonic() - before, .8)
                self.assertFalse(errors, errors)
                self.assertEqual(results[0]['status'], 'cancelled')
            finally:
                stop.set()
                worker.join(timeout=2)
                receivers.close()

    def test_slow_receiver_does_not_stop_other_cameras(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            config, manifest = fixture(Path(temporary), size=(1024, 1024))
            receivers = CameraReceivers(config, stalled='payload')
            try:
                with self.assertRaises(ProducerError) as raised:
                    stream(config, manifest, loops=3, reconnect_attempts=0,
                        timeout_ms=50, max_lateness_ms=250)
                cameras = raised.exception.report['cameras']
                self.assertEqual(cameras[0]['status'], 'failed')
                self.assertEqual(cameras[0]['transport_failures'], 1)
                self.assertGreaterEqual(cameras[0]['max_send_block_ms'], 30)
                for camera in cameras[1:]:
                    self.assertEqual(camera['status'], 'completed')
                    self.assertGreater(camera['sent_frames'], 0)
                    self.assertEqual(camera['transport_failures'], 0)
            finally:
                receivers.close()

    def test_invalid_handshake_is_fatal_without_retry(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            config, manifest = fixture(Path(temporary))
            receivers = CameraReceivers(config, stalled='invalid')
            try:
                with self.assertRaises(ProducerError) as raised:
                    stream(config, manifest, loops=3, timeout_ms=100)
                cameras = raised.exception.report['cameras']
                self.assertEqual(cameras[0]['connect_attempts'], 1)
                self.assertEqual(cameras[0]['connections'], 0)
                self.assertEqual(cameras[0]['transport_failures'], 0)
                self.assertIn('handshake rejected', cameras[0]['last_error'])
                self.assertTrue(all(c['status'] == 'completed' for c in cameras[1:]))
            finally:
                receivers.close()

    def test_handshake_deadline_survives_slow_drip(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            config, manifest = fixture(Path(temporary))
            receivers = CameraReceivers(config, stalled='drip')
            try:
                before = time.monotonic()
                with self.assertRaises(ProducerError) as raised:
                    stream(config, manifest, loops=3, reconnect_attempts=0, timeout_ms=100)
                self.assertLess(time.monotonic() - before, 1.)
                cameras = raised.exception.report['cameras']
                self.assertEqual(cameras[0]['status'], 'failed')
                self.assertEqual(cameras[0]['transport_failures'], 1)
                self.assertEqual(cameras[0]['connections'], 0)
                self.assertTrue(all(c['status'] == 'completed' for c in cameras[1:]))
            finally:
                receivers.close()

    def test_sigterm_preserves_cancelled_report(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            directory = Path(temporary)
            config, manifest = fixture(directory)
            config_path = directory / 'config.json'
            config_path.write_text(json.dumps(config))
            report_path = directory / 'cancelled.json'
            receivers = CameraReceivers(config, stalled='handshake')
            process = subprocess.Popen([sys.executable, str(Path(__file__).resolve().parents[1]/'tools/producer.py'),
                '--config', str(config_path), '--manifest', str(manifest), '--loops', '1000',
                '--timeout-ms', '100', '--report', str(report_path)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertTrue(receivers.handshake.wait(2))
                process.terminate()
                _, error = process.communicate(timeout=2)
                self.assertEqual(process.returncode, 143, error)
                self.assertEqual(json.loads(report_path.read_text())['status'], 'cancelled')
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=2)
                receivers.close()

    def test_report_refusal_and_numeric_tcp_destinations(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            directory = Path(temporary)
            config, manifest = fixture(directory)
            path = directory / 'report.json'
            path.write_text('existing report')
            with self.assertRaisesRegex(ValueError, 'report already exists'):
                stream(config, manifest, report_path=path)
            self.assertEqual(path.read_text(), 'existing report')
            sidecar = directory / 'other.md'
            sidecar.write_text('existing markdown')
            with self.assertRaisesRegex(ValueError, 'report already exists'):
                stream(config, manifest, report_path=directory / 'other.json')
            self.assertFalse((directory / 'other.json').exists())
            self.assertEqual(sidecar.read_text(), 'existing markdown')
            config['source']['cameras'][0] = dict(camera_id=0, transport='tcp', address='localhost', port=12345)
            with self.assertRaises(ValueError):
                stream(config, manifest)
            with self.assertRaisesRegex(ValueError, 'limits'):
                stream(config, manifest, reconnect_attempts=-1)


if __name__ == '__main__':
    unittest.main()
