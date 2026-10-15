"""Live opt-in telemetry: hot settings, cursors, read-only lease and typed transports."""
import json
import socket
import subprocess
import sys
import time
import unittest

import test_output_products as output

BUILD, Client = output.BUILD, output.Client


class Telemetry(unittest.TestCase):
    # Reuse only lifecycle helpers, not the output-product test cases.
    setUp = output.OutputProducts.setUp
    shutdown = output.OutputProducts.shutdown
    start = output.OutputProducts.start

    def catalog(self):
        ack = self.client.command('telemetry_catalog')
        self.assertTrue(ack['accepted'], ack)
        return ack['telemetry_catalog']

    def read(self, catalog, after='0', limit=16, **overrides):
        params = dict(server_instance_id=catalog['server_instance_id'],
                      epoch=catalog['epoch'], after_cursor=after, limit=limit)
        params.update(overrides)
        return self.client.command('telemetry_read', **params)

    def apply(self, revision, enabled, **settings):
        return self.client.command('config_apply', base_config_revision=str(revision),
            changes={'telemetry': dict(enabled=enabled, **settings)}, persist=False)

    def test_hot_enable_sampling_while_paused_and_disable(self):
        self.start()
        self.client = Client(self.ipc)
        self.client.frame()
        original = self.config.read_bytes()
        self.client.command('pause')
        disabled = self.catalog()
        self.assertFalse(disabled['settings']['enabled'])
        self.assertEqual(self.read(disabled)['telemetry']['samples'], [])
        intent = dict(server_instance_id=disabled['server_instance_id'],
                      operation_id='telemetry-enable-test-0001', base_config_revision='0',
                      changes={'telemetry': dict(enabled=True, period_ms=50, history_capacity=8)},
                      persist=False)
        enabled = self.client.command('config_apply_once', **intent)
        self.assertTrue(enabled['accepted'], enabled)
        catalog = self.catalog()
        self.assertNotEqual(catalog['epoch'], disabled['epoch'])
        replayed = self.client.command('config_apply_once', **intent)
        self.assertTrue(replayed['configuration_operation']['replayed'])
        self.assertEqual(self.catalog()['epoch'], catalog['epoch'])
        # Stop receiving frames: process sampling must survive paused/busy output.
        time.sleep(.65)
        first = self.read(catalog, limit=3)
        self.assertTrue(first['accepted'], first)
        batch = first['telemetry']
        self.assertGreater(int(batch['lost_samples']), 0)
        self.assertEqual(len(batch['samples']), 3)
        self.assertTrue(batch['has_more'])
        self.assertTrue(all(s['paused'] for s in batch['samples']))
        self.assertTrue(all(s['process']['cpu_percent'] is not None for s in batch['samples']))
        self.assertTrue(all(int(s['process']['rss_bytes']) > 0 for s in batch['samples']))
        second = self.read(catalog, batch['next_cursor'])['telemetry']
        self.assertGreater(int(second['samples'][0]['cursor']), int(batch['next_cursor']))
        before = self.client.command('state')
        for extra in ({'epoch': disabled['epoch']}, {'server_instance_id': 'foreign'},
                      {'after_cursor': str(2**64 - 1)}, {'limit': True}, {'limit': 17},
                      {'limit': 2**64 - 1}):
            rejected = self.read(catalog, **extra)
            self.assertFalse(rejected['accepted'], rejected)
        invalid = self.apply(1, True, period_ms=1)
        self.assertFalse(invalid['accepted'], invalid)
        self.assertEqual(self.catalog()['epoch'], catalog['epoch'])
        self.assertEqual(self.client.command('state')['config_revision'], before['config_revision'])
        self.assertTrue(self.apply(1, False)['accepted'])
        stopped = self.catalog()
        self.assertFalse(stopped['settings']['enabled'])
        time.sleep(.1)
        self.assertEqual(self.read(stopped)['telemetry']['latest_cursor'], '0')
        self.assertEqual(self.config.read_bytes(), original)

    def test_persistent_settings_survive_restart_with_new_instance(self):
        self.start()
        self.client = Client(self.ipc)
        previous = self.catalog()
        saved = self.client.command('config_apply', base_config_revision='0',
            changes={'telemetry': dict(enabled=True, period_ms=50, history_capacity=8)},
            persist=True)
        self.assertTrue(saved['accepted'], saved)
        self.assertTrue(json.loads(self.config.read_text())['telemetry']['enabled'])
        self.shutdown()
        self.start()
        self.client = Client(self.ipc)
        current = self.catalog()
        self.assertTrue(current['settings']['enabled'])
        self.assertNotEqual(current['server_instance_id'], previous['server_instance_id'])
        self.assertFalse(self.read(previous)['accepted'])
        self.assertTrue(self.read(current)['accepted'])

    def test_persistence_failure_does_not_publish_prepared_sampler(self):
        self.start()
        self.client = Client(self.ipc)
        before = self.catalog()
        self.config.unlink()
        self.config.mkdir()  # Deterministic atomic rename failure, after preparation.
        failed = self.client.command('config_apply', base_config_revision='0',
            changes={'telemetry': {'enabled': True}}, persist=True)
        self.assertFalse(failed['accepted'], failed)
        self.assertEqual(self.catalog(), before)
        self.assertEqual(self.client.command('state')['config_revision'], '0')
        self.assertEqual(self.read(before)['telemetry']['samples'], [])

    def test_reads_during_lease_do_not_change_revisions_or_configuration(self):
        self.cfg['telemetry'] = dict(enabled=True, period_ms=50, history_capacity=8)
        self.config.write_text(json.dumps(self.cfg))
        self.start()
        self.client = Client(self.ipc)
        self.client.frame()
        self.client.command('pause')
        lease = self.client.command('experiment_acquire', ttl_ms=3000)
        self.assertTrue(lease['accepted'], lease)
        before = self.client.command('state')
        original = self.config.read_bytes()
        catalog = self.catalog()
        self.assertTrue(self.read(catalog)['accepted'])
        forbidden = self.apply(0, False)
        self.assertFalse(forbidden['accepted'], forbidden)
        after = self.client.command('state')
        self.assertEqual(after['state_revision'], before['state_revision'])
        self.assertEqual(after['config_revision'], before['config_revision'])
        self.assertEqual(self.config.read_bytes(), original)

    def test_typed_consumer_over_unix_and_tcp(self):
        reservations = [socket.socket(), socket.socket()]
        try:
            for item in reservations:
                item.bind(('127.0.0.1', 0))
            ctl, data = [item.getsockname()[1] for item in reservations]
        finally:
            for item in reservations:
                item.close()
        self.cfg['telemetry'] = dict(enabled=True, period_ms=50, history_capacity=8)
        self.cfg['connections'] = {
            'unix': {'enabled': True, 'directory': str(self.ipc)},
            'tcp': {'enabled': True, 'address': '127.0.0.1', 'control_port': ctl, 'data_port': data}}
        self.config.write_text(json.dumps(self.cfg))
        self.start()
        time.sleep(.1)
        for endpoint in (['--unix', str(self.ipc)], ['--tcp', '127.0.0.1', str(ctl), str(data)]):
            result = subprocess.run([str(BUILD / 'sv-client-probe'), *endpoint,
                '--frames', '1', '--telemetry'], text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            messages = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertTrue(any(m.get('validated_telemetry_batch') for m in messages))
            self.assertTrue(any('telemetry_v1' in m.get('capabilities', []) for m in messages))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
