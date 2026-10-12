"""Live server output selection, native mask parity and typed C++ consumer."""
import copy
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ipc import Client, receive
from simulator import generate

BUILD, CONFIG = [Path(p).resolve() for p in sys.argv[1:3]]


class OutputProducts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='sv-output-')
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.cfg = json.loads(CONFIG.read_text())
        self.cfg['output'].update(width=96, height=64)
        self.config = self.directory / 'config.json'
        self.config.write_text(json.dumps(self.cfg))
        self.manifest = generate(self.directory / 'fixture', self.cfg, 1)
        self.ipc = self.directory / 'ipc'
        self.server = None
        self.client = None
        self.addCleanup(self.shutdown)

    def shutdown(self):
        if self.client:
            self.client.close()
            self.client = None
        if self.server:
            self.server.terminate()
            _, stderr = self.server.communicate(timeout=5)
            if self.server.returncode not in (0, -15):
                raise RuntimeError(stderr)
            self.server = None

    def start(self):
        command = [str(BUILD / 'sv-server'), '--config', str(self.config),
                   '--manifest', str(self.manifest)]
        if 'connections' not in self.cfg:
            command += ['--ipc-dir', str(self.ipc)]
        self.server = subprocess.Popen(command,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 8
        while not (self.ipc / 'data.sock').exists():
            if self.server.poll() is not None:
                raise RuntimeError(self.server.communicate()[1])
            if time.monotonic() > deadline:
                raise TimeoutError('startup')
            time.sleep(.01)

    def frame_after(self, ack):
        self.assertTrue(ack['accepted'], ack)
        for _ in range(10):
            header, pixels = self.client.frame()
            if int(header['state_revision']) >= int(ack['state_revision']):
                return header, pixels
        self.fail('frame revision did not advance')

    def test_live_selection_native_parity_rejections_and_reconnect(self):
        self.start()
        self.client = Client(self.ipc)
        self.client.frame()
        first, final = self.frame_after(self.client.command('pause'))
        original = self.config.read_bytes()
        state = self.client.command('state')
        catalog = self.client.command('output_catalog')
        self.assertTrue(catalog['accepted'])
        self.assertEqual(catalog['state_revision'], state['state_revision'])
        self.assertEqual(catalog['output_catalog']['products'], ['final_frame', 'render_support_v1'])
        ack = self.client.command('configure_output', product='render_support_v1')
        self.assertEqual(ack['config_revision'], state['config_revision'])
        header, mask = self.frame_after(ack)
        self.assertEqual(header['output_product'], 'render_support_v1')
        self.assertEqual(header['applied_command_id'], ack['command_id'])
        self.assertEqual(header['inputs'], first['inputs'])
        self.assertEqual(header['frame_set_id'], first['frame_set_id'])
        self.assertEqual(header['upload_count'], first['upload_count'])
        self.assertGreater(header['inspection_cpu_ms'], 0)
        pixels = np.frombuffer(mask, np.uint8).reshape(64, 96, 4)
        self.assertTrue(np.all(pixels[..., 0] <= 4))
        self.assertTrue(np.all(pixels[..., 1] <= pixels[..., 0]))
        self.assertGreater(np.count_nonzero(pixels[..., 0]), 0)
        self.assertTrue(np.all(pixels[..., 2] <= 6))

        for bad in ('unknown_v2', 7, None):
            rejected = self.client.command('configure_output', product=bad)
            self.assertFalse(rejected['accepted'])
            self.assertEqual(rejected['state_revision'], ack['state_revision'])
            self.assertEqual(rejected['output_product'], 'render_support_v1')
        fusion = copy.deepcopy(ack['fusion'])
        fusion['diagnostic'] = 'coverage'
        rejected = self.client.command('configure_fusion',
            base_config_revision=ack['config_revision'], fusion=fusion)
        self.assertFalse(rejected['accepted'])
        self.assertEqual(rejected['reason'], 'render_support_requires_color')
        self.assertEqual(rejected['config_revision'], ack['config_revision'])
        self.assertEqual(rejected['diagnostic_view'], 'color')

        # Compare complete wire masks with the same native renderer invocation,
        # rather than checking only histogram totals or formatted headers.
        for mode in ('edge_feather', 'angular_feather', 'hard_best_angle',
                     'seam_distance_feather', 'graph_cut_seam', 'multi_band', 'graph_cut_multi_band'):
            with self.subTest(mode=mode):
                active = self.client.command('state')
                fusion = copy.deepcopy(active['fusion'])
                fusion['mode'] = mode
                ack = self.client.command('configure_fusion',
                    base_config_revision=active['config_revision'], fusion=fusion)
                header, mask = self.frame_after(ack)
                cfg = copy.deepcopy(self.cfg)
                cfg['fusion'] = ack['fusion']
                native_config = self.directory / 'native.json'
                native_config.write_text(json.dumps(cfg))
                native_mask = self.directory / 'native.rgba'
                result = subprocess.run([str(BUILD / 'sv-support-wire-probe'), str(native_config),
                    str(self.manifest), str(native_mask)], capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(mask, native_mask.read_bytes())
                self.assertEqual(header['config_revision'], ack['config_revision'])
                self.assertEqual(header['inputs'], first['inputs'])

        lease = self.client.command('experiment_acquire', ttl_ms=5000)
        self.assertTrue(lease['accepted'])
        self.assertTrue(self.client.command('output_catalog')['accepted'])
        rejected = self.client.command('configure_output', product='final_frame',
                                       lease_id=lease['experiment_lease']['lease_id'])
        self.assertFalse(rejected['accepted'])
        self.client.command('experiment_release', lease_id=lease['experiment_lease']['lease_id'])
        deadline = time.monotonic() + 3
        while self.client.command('state')['experiment_lease']['state'] != 'idle':
            if time.monotonic() > deadline:
                self.fail('lease restore timeout')
            time.sleep(.02)
        header, _ = self.frame_after(self.client.command('configure_output', product='final_frame'))
        self.assertNotIn('support_schema', header)
        # Return to the initial fusion mode before comparing normal output bytes.
        active = self.client.command('state')
        active['fusion']['mode'] = 'edge_feather'
        header, restored = self.frame_after(self.client.command('configure_fusion',
            base_config_revision=active['config_revision'], fusion=active['fusion']))
        self.assertEqual(restored, final)
        self.assertEqual(self.config.read_bytes(), original)

        # Reject inverse conflict without changing output selection.
        active = self.client.command('state')
        active['fusion']['diagnostic'] = 'coverage'
        self.frame_after(self.client.command('configure_fusion',
            base_config_revision=active['config_revision'], fusion=active['fusion']))
        rejected = self.client.command('configure_output', product='render_support_v1')
        self.assertFalse(rejected['accepted'])
        self.assertEqual(rejected['output_product'], 'final_frame')
        active = self.client.command('state')
        active['fusion']['diagnostic'] = 'color'
        self.frame_after(self.client.command('configure_fusion',
            base_config_revision=active['config_revision'], fusion=active['fusion']))
        self.frame_after(self.client.command('configure_output', product='render_support_v1'))
        # A held diagnostic slot follows the same release timeout as a normal frame.
        self.client.command('preset', name='top')
        self.assertEqual(receive(self.client.data)[0], 11)
        self.assertTrue(self.client.command('state')['accepted'])
        time.sleep(.35)
        with self.assertRaises(EOFError):
            receive(self.client.data)
        self.client.close()
        self.client = None
        self.client = Client(self.ipc)
        header, _ = self.client.frame()
        self.assertEqual(header['output_product'], 'final_frame')
        self.assertNotIn('support_schema', header)

    def test_reconnect_during_pending_support_does_not_leak_product(self):
        self.start()
        for _ in range(10):
            self.client = Client(self.ipc)
            header, _ = self.client.frame()
            self.assertEqual(header['output_product'], 'final_frame')
            ack = self.client.command('configure_output', product='render_support_v1')
            self.assertTrue(ack['accepted'])
            # Close immediately after ACK, without waiting for or releasing the mask.
            # An old render/queued publication must never enter the next data generation.
            self.client.close()
            self.client = None
        self.client = Client(self.ipc)
        header, _ = self.client.frame()
        self.assertEqual(header['output_product'], 'final_frame')

    def test_typed_cpp_consumer_unix_and_tcp(self):
        reservations = [socket.socket(), socket.socket()]
        try:
            for item in reservations:
                item.bind(('127.0.0.1', 0))
            ctl, data = [item.getsockname()[1] for item in reservations]
        finally:
            for item in reservations:
                item.close()
        self.cfg['connections'] = {
            'unix': {'enabled': True, 'directory': str(self.ipc)},
            'tcp': {'enabled': True, 'address': '127.0.0.1', 'control_port': ctl, 'data_port': data}}
        self.config.write_text(json.dumps(self.cfg))
        self.start()
        for endpoint in (['--unix', str(self.ipc)], ['--tcp', '127.0.0.1', str(ctl), str(data)]):
            result = subprocess.run([str(BUILD / 'sv-client-probe'), *endpoint,
                '--frames', '1', '--support'], text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            messages = [json.loads(line) for line in result.stdout.splitlines()]
            frames = [m for m in messages if m.get('message_type') == 11]
            self.assertGreaterEqual(len(frames), 1)
            for frame in frames:
                self.assertEqual(frame['output_product'], 'render_support_v1')
                self.assertGreater(frame['eligible_pixels'], 0)
            hello = next(m for m in messages if m.get('message_type') == 2)
            self.assertIn('output_selection_v1', hello['capabilities'])
            self.assertIn('render_support_v1', hello['capabilities'])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
