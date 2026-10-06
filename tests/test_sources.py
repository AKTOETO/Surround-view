import json
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ipc import Client, pack, receive
from simulator import generate
from producer import stream
BUILD, CONFIG = Path(sys.argv[1]), Path(sys.argv[2])


class SocketSources(unittest.TestCase):
    def exercise(self, transport):
        with tempfile.TemporaryDirectory(prefix='sv-cameras-') as temporary:
            directory = Path(temporary)
            config = json.loads(CONFIG.read_text())
            config['runtime']['skew_window_ms'] = 80
            config['runtime']['max_input_age_ms'] = 200
            ipc = directory / 'client'
            config['connections'] = dict(unix=dict(enabled=True, directory=str(ipc)))
            endpoints, reservations = [], []
            for camera_id in range(4):
                endpoint = dict(camera_id=camera_id, transport=transport)
                if transport == 'unix':
                    endpoint['path'] = str(directory / f'camera{camera_id}.sock')
                else:
                    reservation = socket.socket()
                    reservation.bind(('127.0.0.1', 0))
                    endpoint.update(address='127.0.0.1', port=reservation.getsockname()[1])
                    reservations.append(reservation)
                endpoints.append(endpoint)
            config['source'] = dict(type='socket', message_timeout_ms=150, cameras=endpoints)
            config_path = directory / 'config.json'
            config_path.write_text(json.dumps(config))
            for reservation in reservations:
                reservation.close()
            server = subprocess.Popen([str(BUILD / 'sv-server'), '--config', str(config_path),
                '--trace', str(directory / 'trace.jsonl')], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            client, cameras = None, []
            try:
                deadline = time.monotonic() + 8
                while not (ipc / 'data.sock').exists():
                    if server.poll() is not None:
                        raise RuntimeError(server.communicate()[1])
                    if time.monotonic() > deadline:
                        raise TimeoutError('startup')
                    time.sleep(.01)
                client = Client(ipc)
                self.assertFalse(client.command('step')['accepted'])
                self.assertEqual(client.frame()[0]['health'], 'NO_INPUT')

                def connect(camera_id, calibration=None):
                    endpoint = endpoints[camera_id]
                    if transport == 'unix':
                        sock = socket.socket(socket.AF_UNIX)
                        sock.settimeout(2)
                        sock.connect(endpoint['path'])
                    else:
                        sock = socket.create_connection((endpoint['address'], endpoint['port']), timeout=2)
                    camera = config['cameras'][camera_id]
                    hello = dict(role='producer', camera_id=camera_id,
                        calibration_id=calibration or camera['calibration_id'], **camera['resolution'],
                        pixel_format='RGB8', row_origin='top_left', clock_domain='test_clock')
                    sock.sendall(pack(1, hello))
                    return sock

                sessions = []
                for i in range(4):
                    cameras.append(connect(i))
                    kind, hello, _ = receive(cameras[-1])
                    self.assertEqual(kind, 2)
                    sessions.append(hello['session_id'])
                sequence = [0] * 4

                def send(ids=range(4), duplicate=False):
                    for i in ids:
                        camera = config['cameras'][i]
                        resolution = camera['resolution']
                        header = dict(session_id=sessions[i], camera_id=i,
                            calibration_id=camera['calibration_id'], **resolution,
                            stride_bytes=resolution['width'] * 3, pixel_format='RGB8', row_origin='top_left',
                            clock_domain='test_clock', sequence_id=str(sequence[i]),
                            source_timestamp_ns=str(sequence[i] * 1000), scenario_timestamp_ns='0')
                        cameras[i].sendall(pack(10, header, bytes([30 + i * 40]) * (resolution['width'] * resolution['height'] * 3)))
                        if not duplicate:
                            sequence[i] += 1

                def frame_when(predicate):
                    deadline = time.monotonic() + 4
                    while time.monotonic() < deadline:
                        header, _ = client.frame()
                        if predicate(header):
                            return header
                    raise TimeoutError('expected frame state')

                send()
                header = frame_when(lambda h: h['health'] == 'READY')
                self.assertEqual(header['source_type'], 'socket')
                self.assertEqual(header['timestamp_basis'], 'server_delivery')
                for i, source in enumerate(header['inputs']):
                    self.assertEqual(source['source_session_id'], sessions[i])
                    self.assertEqual(source['source_clock_domain'], 'test_clock')
                ack = client.command('pause')
                self.assertTrue(ack['accepted'])
                header = frame_when(lambda h: int(h['state_revision']) >= int(ack['state_revision']))
                uploads = header['upload_count']
                for _ in range(8):
                    send()
                time.sleep(.05)
                ack = client.command('orbit', azimuth_delta_rad=.1, elevation_delta_rad=0.)
                header = frame_when(lambda h: int(h['state_revision']) >= int(ack['state_revision']))
                self.assertEqual(header['upload_count'], uploads)
                self.assertGreater(int(header['source_dropped_batches']), 0)
                self.assertTrue(client.command('resume')['accepted'])
                send()
                frame_when(lambda h: h['health'] == 'READY' and int(h['source_received']) >= 40)
                # Duplicate sequence rejects one camera only. Others remain connected.
                sequence[0] -= 1
                send([0], duplicate=True)
                self.assertEqual(cameras[0].recv(1), b'')
                cameras[0].close()
                time.sleep(.22)
                send([1, 2, 3])
                header = frame_when(lambda h: h['health'] == 'DEGRADED' and int(h['source_rejected']) >= 1)
                self.assertFalse(header['inputs'][0]['used'])
                cameras[0] = connect(0)
                _, hello, _ = receive(cameras[0])
                self.assertNotEqual(hello['session_id'], sessions[0])
                sessions[0] = hello['session_id']
                sequence[0] = 0
                send()
                header = frame_when(lambda h: h['health'] == 'READY' and h['inputs'][0]['source_session_id'] == sessions[0])
                self.assertEqual(header['inputs'][0]['source_sequence_id'], '0')
                client.close()
                client = None
                # Wrong calibration and a partial-handshake timeout affect camera zero only.
                cameras[0].close()
                time.sleep(.03)
                bad = connect(0, 'wrong-calibration')
                self.assertEqual(bad.recv(1), b'')
                bad.close()
                time.sleep(.03)
                endpoint = endpoints[0]
                if transport == 'unix':
                    bad = socket.socket(socket.AF_UNIX)
                    bad.connect(endpoint['path'])
                else:
                    bad = socket.create_connection((endpoint['address'], endpoint['port']))
                bad.settimeout(2)
                bad.sendall(b'SV')
                self.assertEqual(bad.recv(1), b'')
                bad.close()
                # Exercise the actual Blender/replay producer tool, including digest validation.
                for camera in cameras:
                    camera.close()
                cameras = []
                time.sleep(.03)
                manifest = generate(directory / 'fixture', config, 8)
                client = Client(ipc)
                self.assertTrue(client.command('state')['accepted'])
                producer = subprocess.Popen([sys.executable, str(ROOT / 'tools/producer.py'),
                    '--config', str(config_path), '--manifest', str(manifest), '--loops', '2'],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    frame_when(lambda h: h['health'] == 'READY' and all(
                        i.get('source_clock_domain') == 'producer_monotonic' for i in h['inputs']))
                    _, error = producer.communicate(timeout=5)
                    self.assertEqual(producer.returncode, 0, error)
                finally:
                    if producer.poll() is None:
                        producer.terminate()
                        producer.communicate(timeout=5)
            finally:
                for camera in cameras:
                    camera.close()
                if client:
                    client.close()
                server.terminate()
                _, error = server.communicate(timeout=5)
                self.assertIn(server.returncode, (0, -15), error)
            if transport == 'unix':
                self.assertFalse(any(Path(e['path']).exists() for e in endpoints))

    def test_producer_validates_before_connecting(self):
        with tempfile.TemporaryDirectory(prefix='sv-producer-') as temporary:
            config = json.loads(CONFIG.read_text())
            config['source'] = dict(type='socket', cameras=[dict(camera_id=i,
                transport='unix', path='/nonexistent/camera' + str(i)) for i in range(4)])
            path = generate(Path(temporary), config, 1)
            original = path.read_text()
            manifest = json.loads(original)
            manifest['sha256'][manifest['frames'][0]['paths'][0]] = '0' * 64
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'checksum'):
                stream(config, path)
            manifest = json.loads(original)
            manifest['frames'][0]['paths'][0] = '../outside.ppm'
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'escapes'):
                stream(config, path)
            manifest = json.loads(original)
            manifest['frames'][0]['offset_ns'][0] = 1
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'offsets'):
                stream(config, path)

    def test_unix_sources(self):
        self.exercise('unix')

    def test_tcp_sources(self):
        self.exercise('tcp')


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
