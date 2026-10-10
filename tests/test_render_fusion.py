"""Full hybrid composition oracle, analytic plane sampling, and actual SV01 parity."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from fusion import fuse_samples
from ipc import Client
import reference

PROBE, CONFIG, SERVER = map(lambda p: Path(p).resolve(), sys.argv[1:4])


class RenderFusionParity(unittest.TestCase):
    def fixture(self, directory):
        cfg = json.loads(CONFIG.read_text())
        cfg['output'].update(width=63, height=47)
        cfg['fusion'] = dict(mode='edge_feather', pyramid_levels=3, smoothness_weight=.7)
        paths, hashes, images = [], {}, []
        for camera in cfg['cameras']:
            w, h = camera['resolution']['width'], camera['resolution']['height']
            y, x = np.mgrid[:h, :w]
            # Asymmetric smooth RGB gradients expose row/axis swaps and camera ordering.
            c = camera['id']
            rgb = np.uint8(np.stack([20 + 100*x/(w-1) + 17*c,
                25 + 150*y/(h-1), 15 + 65*(x/(w-1)+y/(h-1)) + 11*c], axis=-1))
            name = f'camera{c}.ppm'
            Image.fromarray(rgb).save(directory/name)
            paths.append(name)
            hashes[name] = hashlib.sha256((directory/name).read_bytes()).hexdigest()
            images.append(rgb)
        manifest = dict(schema_version=1, calibration_ids=[c['calibration_id'] for c in cfg['cameras']],
            sha256=hashes, frames=[dict(paths=paths, scenario_timestamp_ns='0', offset_ns=[0]*4)])
        (directory/'manifest.json').write_text(json.dumps(manifest))
        return cfg, images

    def reconstructed(self, directory, index, cfg, case):
        h, w = index['height'], index['width']
        endian = '<f4' if index['float_endian'] == 'little' else '>f4'
        colors, validity, edges = [], [], []
        for c in range(4):
            prefix = directory/f'camera{c}'
            colors.append(np.fromfile(str(prefix)+'-linear.f32', endian).reshape(h, w, 3))
            validity.append(np.fromfile(str(prefix)+'-valid.u8', np.uint8).reshape(h, w) != 0)
            edges.append(np.fromfile(str(prefix)+'-edge.f32', endian).reshape(h, w))
        colors, validity, edges = (np.stack(a, axis=2) for a in [colors, validity, edges])
        fused, weights = fuse_samples(colors, validity, np.zeros_like(edges), edges*24,
            np.zeros((h, w, 3)), mode=case['mode'], num_pyramid_levels=3, smoothness_weight=.7)
        rgb = np.where(fused <= .0031308, 12.92*fused,
                       1.055*np.maximum(fused, 0)**(1/2.4)-.055)
        if case['diagnostic'] == 'weights':
            rgb = np.stack([weights[..., 0]+weights[..., 3], weights[..., 1]+weights[..., 3],
                            weights[..., 2]], axis=-1)
        elif case['diagnostic'] == 'coverage':
            rgb = np.repeat((validity.sum(axis=-1)/4)[..., None], 3, axis=-1)
        expected = np.fromfile(directory/'fallback.rgba', np.uint8).reshape(h, w, 4).copy()
        observed = validity.any(axis=-1)
        # C++ lround for nonnegative values, distinct from NumPy ties-to-even.
        expected[observed, :3] = np.floor(np.clip(rgb[observed], 0, 1)*255+.5).astype(np.uint8)
        if (directory/'ego.rgba').exists():
            ego = np.fromfile(directory/'ego.rgba', np.uint8).reshape(h, w, 4)
            expected[ego[..., 3] != 0] = ego[ego[..., 3] != 0]
        actual = np.fromfile(directory/'actual.rgba', np.uint8).reshape(h, w, 4)
        error = np.abs(actual.astype(int)-expected.astype(int))
        self.assertLessEqual(error.max(), 1, (cfg['surface']['type'], case, error.max()))
        self.assertTrue((actual[..., 3] == 255).all())
        return colors, validity, edges

    def plane_sampling(self, cfg, images, samples):
        colors, validity, edges = samples
        eye, rays, depth = reference.rays(cfg)
        points, hit, _ = reference.intersect(cfg['surface'], eye, rays,
                                            *cfg['virtual_camera']['clip_m'], depth)
        vehicle = cfg['vehicle']
        body = ((np.abs(points[..., 0]) <= vehicle['length_m']/2+vehicle['mask_margin_m']) &
                (np.abs(points[..., 1]) <= vehicle['width_m']/2+vehicle['mask_margin_m']))
        encoded = np.where(colors <= .0031308, colors*12.92,
                           1.055*np.maximum(colors, 0)**(1/2.4)-.055)
        checked = 0
        for c, (camera, image) in enumerate(zip(cfg['cameras'], images)):
            rgb, valid, edge, theta = reference.sample(camera, points, image)
            expected_valid = valid & hit & ~body
            np.testing.assert_array_equal(validity[..., c], expected_valid)
            checked += expected_valid.sum()
            self.assertLessEqual(np.max(np.abs(encoded[..., c, :][expected_valid] -
                                              rgb[expected_valid])), 2/255)
            np.testing.assert_allclose(edges[..., c][expected_valid],
                np.clip(edge[expected_valid]/24, 0, 1), atol=1/254+1e-5, rtol=0)
        self.assertGreater(checked, 100)

    def server_parity(self, directory, cfg, cases, output):
        ipc = directory/'ipc'
        config = directory/'server.json'
        cfg['connections'] = dict(unix=dict(enabled=True, directory=str(ipc)),
                                  tcp=dict(enabled=False), udp=dict(enabled=False))
        config.write_text(json.dumps(cfg))
        server = subprocess.Popen([str(SERVER), '--config', str(config), '--manifest',
            str(directory/'manifest.json'), '--trace', str(directory/'trace.jsonl')],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        client = None
        try:
            deadline = time.monotonic()+8
            while not (ipc/'data.sock').exists():
                if server.poll() is not None:
                    self.fail(server.communicate()[1])
                self.assertLess(time.monotonic(), deadline, 'startup timeout')
                time.sleep(.02)
            client = Client(ipc)
            client.frame()
            self.assertTrue(client.command('pause')['accepted'])
            for case in cases:
                state = client.command('state')
                fusion = dict(cfg['fusion'], mode=case['mode'], diagnostic=case['diagnostic'])
                ack = client.command('configure_fusion', base_config_revision=state['config_revision'],
                                     fusion=fusion)
                self.assertTrue(ack['accepted'], ack)
                for _ in range(8):
                    header, rgba = client.frame()
                    if header['state_revision'] == ack['state_revision']:
                        break
                else:
                    self.fail('frame revision mismatch')
                self.assertEqual(rgba, (output/case['directory']/'actual.rgba').read_bytes(), case)
                self.assertEqual(header['gpu_timer_status'], 'hybrid_total_not_measured')
        finally:
            if client:
                client.close()
            server.terminate()
            _, stderr = server.communicate(timeout=5)
            self.assertIn(server.returncode, [0, -15], stderr)

    def test_all_carriers_full_composition_and_plane_wire_parity(self):
        surfaces = json.loads((ROOT/'configs/research/carrier-screen.json').read_text())['variants']
        with tempfile.TemporaryDirectory(prefix='sv-raster-parity-') as td:
            directory = Path(td)
            cfg, images = self.fixture(directory)
            for i, variant in enumerate(surfaces):
                cfg['surface'] = variant['surface']
                config = directory/f'config{i}.json'
                config.write_text(json.dumps(cfg))
                output = directory/f'capture{i}'
                run = subprocess.run([str(PROBE), str(config), str(directory/'manifest.json'), str(output)],
                                     text=True, capture_output=True, timeout=20)
                self.assertEqual(run.returncode, 0, run.stderr)
                index = json.loads((output/'index.json').read_text())
                self.assertEqual(len(index['cases']), 12)
                for case in index['cases']:
                    with self.subTest(carrier=i, **case):
                        samples = self.reconstructed(output/case['directory'], index, cfg, case)
                        if i == 0:
                            self.plane_sampling(cfg, images, samples)
                if i == 0:
                    self.server_parity(directory, cfg, index['cases'], output)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
