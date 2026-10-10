"""Server-side recovery after lease expiry and loss of the control session."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ipc import Client
from simulator import generate

BUILD = Path(sys.argv[1]).resolve()
CONFIG = Path(sys.argv[2]).resolve()


class ExperimentWatchdogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='sv-lease-')
        self.directory = Path(self.temporary.name)
        cfg = json.loads(CONFIG.read_text())
        self.config = self.directory / 'server.json'
        self.config.write_text(json.dumps(cfg))
        self.config_before = self.config.read_bytes()
        manifest = generate(self.directory / 'fixture', cfg, 8)
        self.ipc = self.directory / 'ipc'
        self.server = subprocess.Popen([str(BUILD / 'sv-server'), '--config', str(self.config),
            '--manifest', str(manifest), '--ipc-dir', str(self.ipc),
            '--trace', str(self.directory / 'trace.jsonl')],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.client = None
        deadline = time.monotonic() + 8
        while not (self.ipc / 'data.sock').exists():
            if self.server.poll() is not None:
                raise RuntimeError(self.server.communicate()[1])
            if time.monotonic() >= deadline:
                raise TimeoutError('startup')
            time.sleep(.02)
        self.client = Client(self.ipc)
        self.client.frame()

    def tearDown(self):
        if self.client:
            self.client.close()
        self.server.terminate()
        _, stderr = self.server.communicate(timeout=5)
        try:
            self.assertIn(self.server.returncode, [0, -15], stderr)
            self.assertEqual(self.config.read_bytes(), self.config_before)
        finally:
            self.temporary.cleanup()

    def matching_frame(self, ack):
        for _ in range(10):
            header, pixels = self.client.frame()
            if header['state_revision'] == ack['state_revision']:
                return header, pixels
        self.fail('no matching frame')

    def acquire(self, ttl=30000):
        ack = self.client.command('experiment_acquire', ttl_ms=ttl)
        self.assertTrue(ack['accepted'], ack)
        self.assertEqual(ack['experiment_lease']['state'], 'active')
        return ack['experiment_lease']['lease_id']

    def idle(self):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            state = self.client.command('state')
            if state['experiment_lease']['state'] == 'idle':
                return state
            self.assertNotEqual(state['experiment_lease']['state'], 'failed', state)
            time.sleep(.01)
        self.fail('lease did not restore')

    def change_fusion(self, lease):
        state = self.client.command('state')
        ack = self.client.command('configure_fusion', lease_id=lease,
            base_config_revision=state['config_revision'],
            fusion=dict(mode='angular_feather', diagnostic='weights', angle_power=4))
        self.assertTrue(ack['accepted'], ack)
        self.matching_frame(ack)

    def change_surface(self, lease):
        state = self.client.command('state')
        ack = self.client.command('configure_surface', lease_id=lease,
            base_config_revision=state['config_revision'],
            surface=dict(type='cube_floor_v1', half_extent_m=14, height_m=14, face_cells=8))
        self.assertTrue(ack['accepted'], ack)
        header, _ = self.matching_frame(ack)
        self.assertEqual(header['surface'], ack['surface'])

    def test_expiry_restores_and_rejects_conflicting_commands(self):
        original = self.client.command('state')
        for ttl in [0, 249, 30001, 1.5]:
            rejected = self.client.command('experiment_acquire', ttl_ms=ttl)
            self.assertFalse(rejected['accepted'])
            self.assertEqual(rejected['experiment_lease']['state'], 'idle')
            self.assertEqual(rejected['config_revision'], original['config_revision'])
        lease = self.acquire(500)
        paused = self.client.command('pause', lease_id=lease)
        self.assertTrue(paused['accepted'])
        self.matching_frame(paused)
        self.change_fusion(lease)
        self.change_surface(lease)
        revision = self.client.command('state')['config_revision']
        for command, fields in [
            ('configure_fusion', dict(base_config_revision=revision, fusion=original['fusion'])),
            ('experiment_renew', dict(lease_id='wrong')),
            ('experiment_acquire', dict(ttl_ms=500)),
            ('orbit', dict(lease_id=lease, azimuth_delta_rad=.1, elevation_delta_rad=0)),
            ('apply_calibration', dict(lease_id=lease, job_id='unrelated'))]:
            rejected = self.client.command(command, **fields)
            self.assertFalse(rejected['accepted'], rejected)
            self.assertEqual(rejected['reason'], 'experiment_lease_conflict')
            self.assertEqual(rejected['config_revision'], revision)
        renewed = self.client.command('experiment_renew', lease_id=lease)
        self.assertTrue(renewed['accepted'])
        time.sleep(.6)
        restored = self.idle()
        self.assertEqual(restored['fusion'], original['fusion'])
        self.assertEqual(restored['surface'], original['surface'])
        self.assertEqual(restored['paused'], original['paused'])
        stale = self.client.command('configure_fusion', lease_id=lease,
            base_config_revision=restored['config_revision'], fusion=original['fusion'])
        self.assertFalse(stale['accepted'])
        new_lease = self.acquire()
        self.assertNotEqual(new_lease, lease)
        self.assertTrue(self.client.command('experiment_release', lease_id=new_lease)['accepted'])
        self.idle()

    def disconnect_restore(self, initially_paused):
        if initially_paused:
            self.matching_frame(self.client.command('pause'))
        original = self.client.command('state')
        lease = self.acquire()
        # Change both fusion and source state so restoration cannot pass as a no-op.
        ack = self.client.command('resume' if initially_paused else 'pause', lease_id=lease)
        self.assertTrue(ack['accepted'])
        self.matching_frame(ack)
        self.change_fusion(lease)
        self.change_surface(lease)
        self.client.close()
        self.client = None
        time.sleep(.12)
        self.client = Client(self.ipc)
        restored = self.idle()
        self.assertEqual(restored['fusion'], original['fusion'])
        self.assertEqual(restored['surface'], original['surface'])
        self.assertEqual(restored['paused'], original['paused'])
        self.assertIsNone(self.server.poll())
        # A new session can start a new experiment after recovery.
        lease = self.acquire()
        self.assertTrue(self.client.command('experiment_release', lease_id=lease)['accepted'])
        self.idle()

    def test_disconnect_restores_running_source(self):
        self.disconnect_restore(False)

    def test_disconnect_restores_paused_source(self):
        self.disconnect_restore(True)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
