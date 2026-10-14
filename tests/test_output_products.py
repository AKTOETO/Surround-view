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

    def test_oversized_configuration_read_rejects_without_disconnect_or_mutation(self):
        self.cfg['profile_id'] = 'x' * 20000
        self.config.write_text(json.dumps(self.cfg))
        self.start()
        self.client = Client(self.ipc)
        paused = self.client.command('pause')
        original = self.config.read_bytes()
        rejected = self.client.command('config_read')
        self.assertFalse(rejected['accepted'], rejected)
        self.assertEqual(rejected['reason'], 'configuration_snapshot_too_large')
        self.assertNotIn('configuration', rejected)
        state = self.client.command('state')
        self.assertTrue(state['accepted'])
        self.assertEqual(state['state_revision'], paused['state_revision'])
        self.assertEqual(state['config_revision'], paused['config_revision'])
        self.assertEqual(self.config.read_bytes(), original)

    def test_configuration_read_is_coherent_readonly_and_allowed_during_lease(self):
        self.start()
        self.client = Client(self.ipc)
        self.client.frame()
        paused = self.client.command('pause')
        original = self.config.read_bytes()
        first = self.client.command('config_read')
        self.assertTrue(first['accepted'], first)
        snapshot = first['configuration']
        self.assertEqual(snapshot['schema_version'], 1)
        self.assertEqual(snapshot['scope'], 'config_store')
        self.assertEqual(snapshot['revision'], first['config_revision'])
        self.assertEqual(first['state_revision'], paused['state_revision'])
        self.assertEqual(snapshot['active'], snapshot['persisted'])
        self.assertFalse(snapshot['has_runtime_changes'])
        self.assertTrue(snapshot['persistence_configured'])
        fusion = copy.deepcopy(first['fusion'])
        fusion['mode'] = 'angular_feather'
        changed = self.client.command('configure_fusion',
            base_config_revision=first['config_revision'], fusion=fusion)
        self.assertTrue(changed['accepted'], changed)
        lease = self.client.command('experiment_acquire', ttl_ms=5000)
        self.assertTrue(lease['accepted'], lease)
        active = self.client.command('config_read')  # No lease token for a read.
        self.assertTrue(active['accepted'], active)
        self.assertEqual(active['config_revision'], changed['config_revision'])
        self.assertEqual(active['state_revision'], changed['state_revision'])
        snapshot = active['configuration']
        self.assertEqual(snapshot['revision'], active['config_revision'])
        self.assertTrue(snapshot['has_runtime_changes'])
        self.assertEqual(snapshot['active']['fusion']['mode'], 'angular_feather')
        self.assertEqual(snapshot['persisted'], first['configuration']['persisted'])
        self.assertEqual(self.config.read_bytes(), original)
        self.client.command('experiment_release', lease_id=lease['experiment_lease']['lease_id'])

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
        self.assertEqual(catalog['output_catalog']['products'], ['final_frame', 'render_support_v1', 'source_visibility_v1', 'source_disagreement_v1'])
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

    def test_source_visibility_native_parity_all_modes_and_restore(self):
        self.start()
        self.client = Client(self.ipc)
        self.client.frame()
        first, original = self.frame_after(self.client.command('pause'))
        state = self.client.command('state')
        ack = self.client.command('configure_output', product='source_visibility_v1')
        header, mask = self.frame_after(ack)
        self.assertEqual(header['output_product'], 'source_visibility_v1')
        self.assertEqual(header['source_visibility_schema']['channels'], ['camera0','camera1','camera2','camera3'])
        self.assertNotIn('support_schema', header)
        self.assertEqual(header['inputs'], first['inputs'])
        self.assertEqual(header['config_revision'], state['config_revision'])
        for mode in ('edge_feather', 'angular_feather', 'hard_best_angle',
                     'seam_distance_feather', 'graph_cut_seam', 'multi_band', 'graph_cut_multi_band'):
            active = self.client.command('state')
            fusion = copy.deepcopy(active['fusion'])
            fusion['mode'] = mode
            ack = self.client.command('configure_fusion', base_config_revision=active['config_revision'], fusion=fusion)
            header, mask = self.frame_after(ack)
            pixels = np.frombuffer(mask, np.uint8).reshape(64, 96, 4)
            self.assertTrue(np.all(pixels <= 12))
            self.assertGreater(np.count_nonzero(pixels == 1), 0)
            cfg = copy.deepcopy(self.cfg)
            cfg['fusion'] = ack['fusion']
            native_config = self.directory / 'visibility.json'
            native_config.write_text(json.dumps(cfg))
            native_mask = self.directory / 'visibility.rgba'
            result = subprocess.run([str(BUILD / 'sv-support-wire-probe'), str(native_config),
                str(self.manifest), str(native_mask), 'source_visibility_v1'], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(mask, native_mask.read_bytes())
        for policy, expected_status in [('accept', 10), ('reject', 8)]:
            active = self.client.command('state')
            surface = copy.deepcopy(active['surface'])
            surface['source_occlusion_box'] = {
                'min_m': [-10, -10, -.005], 'max_m': [10, 10, .005], 'tolerance_m': 0,
                'uncertainty': {'radius_m': .01, 'ambiguous_policy': policy}}
            ack = self.client.command('configure_surface', base_config_revision=active['config_revision'], surface=surface)
            header, mask = self.frame_after(ack)
            pixels = np.frombuffer(mask, np.uint8).reshape(64, 96, 4)
            self.assertGreater(np.count_nonzero(pixels == expected_status), 0)
            cfg = copy.deepcopy(self.cfg)
            cfg['fusion'] = ack['fusion']
            cfg['surface'] = ack['surface']
            native_config.write_text(json.dumps(cfg))
            result = subprocess.run([str(BUILD / 'sv-support-wire-probe'), str(native_config),
                str(self.manifest), str(native_mask), 'source_visibility_v1'], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(mask, native_mask.read_bytes())
            self.assertEqual(header['config_revision'], ack['config_revision'])
        active = self.client.command('state')
        ack = self.client.command('configure_surface', base_config_revision=active['config_revision'], surface=state['surface'])
        self.frame_after(ack)
        fusion = copy.deepcopy(ack['fusion'])
        fusion['diagnostic'] = 'coverage'
        rejected = self.client.command('configure_fusion', base_config_revision=ack['config_revision'], fusion=fusion)
        self.assertFalse(rejected['accepted'])
        self.assertEqual(rejected['reason'], 'source_visibility_requires_color')
        self.assertEqual(rejected['config_revision'], ack['config_revision'])
        self.frame_after(self.client.command('configure_output', product='final_frame'))
        active = self.client.command('state')
        active['fusion']['mode'] = 'edge_feather'
        header, restored = self.frame_after(self.client.command('configure_fusion', base_config_revision=active['config_revision'], fusion=active['fusion']))
        self.assertEqual(original, restored)
        self.assertNotIn('source_visibility_schema', header)

    def test_disagreement_native_wire_parity_all_modes_and_color_rejection(self):
        self.start()
        self.client = Client(self.ipc)
        self.client.frame()
        _, original = self.frame_after(self.client.command('pause'))
        initial = self.client.command('state')
        for mode in ('edge_feather', 'angular_feather', 'hard_best_angle',
                     'seam_distance_feather', 'graph_cut_seam', 'multi_band', 'graph_cut_multi_band'):
            state = self.client.command('state')
            ack = self.client.command('configure_fusion', base_config_revision=state['config_revision'], fusion=dict(mode=mode))
            self.frame_after(ack)
            header, data = self.frame_after(self.client.command('configure_output', product='source_disagreement_v1'))
            self.assertNotIn('support_schema', header)
            self.assertNotIn('source_visibility_schema', header)
            cfg = copy.deepcopy(self.cfg)
            cfg['fusion'] = ack['fusion']
            native_config = self.directory/'disagreement.json'
            native_config.write_text(json.dumps(cfg))
            path = self.directory/'disagreement.rgba'
            result = subprocess.run([str(BUILD/'sv-support-wire-probe'), str(native_config), str(self.manifest),
                str(path), 'source_disagreement_v1'], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(data, path.read_bytes())
            p = np.frombuffer(data, np.uint8).reshape(64, 96, 4)
            self.assertTrue(np.all(p[..., 1] <= 4))
            np.testing.assert_array_equal(p[..., 2] == 255, p[..., 1] >= 2)
            self.assertGreater(np.count_nonzero(p[..., 2]), 0)
            self.assertTrue(np.all(p[..., 0][p[..., 1] < 2] == 0))
            rejected = self.client.command('configure_fusion', base_config_revision=header['config_revision'],
                fusion=dict(mode=mode, diagnostic='weights'))
            self.assertFalse(rejected['accepted'])
            self.assertEqual(rejected['reason'], 'source_disagreement_requires_color')
            self.assertEqual(rejected['config_revision'], header['config_revision'])
            self.frame_after(self.client.command('configure_output', product='final_frame'))
        state = self.client.command('state')
        _, restored = self.frame_after(self.client.command('configure_fusion',
            base_config_revision=state['config_revision'], fusion=initial['fusion']))
        self.assertEqual(restored, original)
        state = self.client.command('state')
        self.frame_after(self.client.command('configure_fusion', base_config_revision=state['config_revision'],
            fusion=dict(mode='edge_feather', diagnostic='weights')))
        rejected = self.client.command('configure_output', product='source_disagreement_v1')
        self.assertFalse(rejected['accepted'])
        self.assertEqual(rejected['reason'], 'source_disagreement_requires_color')

    def test_fallback_display_live_switch_masks_rejection_and_reset(self):
        for camera in self.cfg['cameras']:
            camera['projection']['theta_max_rad'] = .05
        self.config.write_text(json.dumps(self.cfg))
        original_file = self.config.read_bytes()
        self.start()
        self.client = Client(self.ipc)
        self.client.frame()
        self.frame_after(self.client.command('pause'))
        catalog = self.client.command('fusion_catalog')['fusion_catalog']
        self.assertEqual(catalog['version'], 5)
        self.assertFalse(catalog['fallback_display']['scene_correspondence'])
        for mode in catalog['modes']:
            with self.subTest(mode=mode):
                active = self.client.command('state')
                fusion = dict(mode=mode)
                header, original = self.frame_after(self.client.command('configure_fusion',
                    base_config_revision=active['config_revision'], fusion=fusion))
                self.assertNotIn('fallback_display', header['fusion'])
                _, support = self.frame_after(self.client.command('configure_output', product='render_support_v1'))
                _, visibility = self.frame_after(self.client.command('configure_output', product='source_visibility_v1'))
                self.frame_after(self.client.command('configure_output', product='final_frame'))
                active = self.client.command('state')
                fusion['fallback_display'] = dict(mode='hatch')
                ack = self.client.command('configure_fusion', base_config_revision=active['config_revision'], fusion=fusion)
                header, marked = self.frame_after(ack)
                self.assertEqual(header['fusion']['fallback_display'], dict(mode='hatch'))
                original_pixels = np.frombuffer(original, np.uint8).reshape(64, 96, 4)
                marked_pixels = np.frombuffer(marked, np.uint8).reshape(64, 96, 4)
                paths = np.frombuffer(support, np.uint8).reshape(64, 96, 4)[..., 2]
                fallback = np.isin(paths, [3, 5])
                self.assertGreater(fallback.sum(), 0)
                np.testing.assert_array_equal(marked_pixels[~fallback], original_pixels[~fallback])
                y, x = np.mgrid[:64, :96]
                dark = ((x // 8 + (63-y) // 8) % 2) != 0
                expected = np.where(dark[..., None], [38, 13, 38, 255], [255, 0, 255, 255])
                self.assertLessEqual(np.abs(marked_pixels.astype(int)[fallback]-expected[fallback]).max(), 1)
                _, actual = self.frame_after(self.client.command('configure_output', product='render_support_v1'))
                self.assertEqual(actual, support)
                _, actual = self.frame_after(self.client.command('configure_output', product='source_visibility_v1'))
                self.assertEqual(actual, visibility)
                for display in [dict(mode='wrong'), {}, dict(mode='hatch', extra=True), 'hatch']:
                    state = self.client.command('state')
                    rejected = self.client.command('configure_fusion', base_config_revision=state['config_revision'],
                        fusion=dict(mode=mode, fallback_display=display))
                    self.assertFalse(rejected['accepted'])
                    self.assertEqual(rejected['config_revision'], state['config_revision'])
                    self.assertEqual(rejected['state_revision'], state['state_revision'])
                    self.assertEqual(rejected['fusion'], state['fusion'])
                self.frame_after(self.client.command('configure_output', product='final_frame'))
                active = self.client.command('state')
                _, restored = self.frame_after(self.client.command('configure_fusion',
                    base_config_revision=active['config_revision'], fusion=dict(mode=mode)))
                self.assertEqual(restored, original)
        self.assertEqual(self.config.read_bytes(), original_file)

    def test_reconnect_during_pending_support_does_not_leak_product(self):
        self.start()
        for attempt in range(10):
            self.client = Client(self.ipc)
            header, _ = self.client.frame()
            self.assertEqual(header['output_product'], 'final_frame')
            ack = self.client.command('configure_output', product=['render_support_v1', 'source_visibility_v1', 'source_disagreement_v1'][attempt % 3])
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
                '--frames', '1', '--configuration'], text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            messages = [json.loads(line) for line in result.stdout.splitlines()]
            snapshots = [m for m in messages if 'validated_configuration_revision' in m]
            self.assertEqual(len(snapshots), 1)
            self.assertEqual(snapshots[0]['validated_configuration_revision'], snapshots[0]['config_revision'])
            self.assertEqual(snapshots[0]['configuration']['active'], snapshots[0]['configuration']['persisted'])
            hellos = [m for m in messages if m.get('message_type') == 2]
            self.assertTrue(any('config_read_v1' in m.get('capabilities', []) for m in hellos))
            result = subprocess.run([str(BUILD / 'sv-client-probe'), *endpoint,
                '--frames', '1', '--support'], text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            messages = [json.loads(line) for line in result.stdout.splitlines()]
            frames = [m for m in messages if m.get('message_type') == 11]
            self.assertGreaterEqual(len(frames), 1)
            for frame in frames:
                self.assertEqual(frame['output_product'], 'render_support_v1')
                self.assertGreater(frame['eligible_pixels'], 0)
            result = subprocess.run([str(BUILD / 'sv-client-probe'), *endpoint,
                '--frames', '1', '--visibility'], text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            visibility_frames = [json.loads(line) for line in result.stdout.splitlines() if json.loads(line).get('message_type') == 11]
            self.assertGreaterEqual(len(visibility_frames), 1)
            for frame in visibility_frames:
                self.assertEqual(frame['output_product'], 'source_visibility_v1')
                self.assertGreater(frame['eligible_camera_pairs'], 0)
            result = subprocess.run([str(BUILD / 'sv-client-probe'), *endpoint,
                '--frames', '1', '--disagreement'], text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            frames = [json.loads(line) for line in result.stdout.splitlines() if json.loads(line).get('message_type') == 11]
            self.assertGreaterEqual(len(frames), 1)
            for frame in frames:
                self.assertEqual(frame['output_product'], 'source_disagreement_v1')
                self.assertGreater(frame['defined_comparison_pixels'], 0)
            hello = next(m for m in messages if m.get('message_type') == 2)
            self.assertIn('output_selection_v1', hello['capabilities'])
            self.assertIn('render_support_v1', hello['capabilities'])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
