"""Native server fusion mathematics vs independent existing NumPy/SciPy path."""
import json
from pathlib import Path
import subprocess
import sys
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from fusion import fuse_samples, compute_graph_cut_seam_mask
PROBE = sys.argv[1]


class NativeFusionParity(unittest.TestCase):
    def compare(self, colors, validity, edges, mode, levels=4, smoothness=.1):
        h, w = validity.shape[:2]
        request = dict(width=w, height=h, colors=colors.reshape(-1).tolist(),
                       validity=validity.reshape(-1).tolist(), edges=edges.reshape(-1).tolist(),
                       mode=mode, levels=levels, smoothness=smoothness)
        run = subprocess.run([PROBE], input=json.dumps(request), text=True,
                             capture_output=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stderr)
        result = json.loads(run.stdout)
        expected, weights = fuse_samples(colors, validity, np.zeros_like(edges), edges * 24,
            np.zeros((h, w, 3)), mode=mode, num_pyramid_levels=levels)
        if mode == 'graph_cut_seam' and smoothness != .1:
            weights = compute_graph_cut_seam_mask(colors, validity, smoothness)
            expected = np.sum(colors * weights[..., None], axis=-2)
        actual = np.array(result['color']).reshape(h, w, 3)
        actual_weights = np.array(result['weights']).reshape(h, w, 4)
        np.testing.assert_allclose(actual_weights, weights, atol=2e-6, rtol=2e-6)
        np.testing.assert_allclose(actual, expected, atol=4e-6, rtol=4e-6)

    def test_reference_parity(self):
        rng = np.random.default_rng(103)
        for shape in [(3, 3), (9, 13), (16, 20)]:
            h, w = shape
            colors = rng.uniform(0, 1, (h, w, 4, 3)).astype(np.float32)
            validity = rng.random((h, w, 4)) > .4
            validity[0, 0] = False
            edges = rng.uniform(0, 1, (h, w, 4)).astype(np.float32)
            for mode in ['seam_distance_feather', 'graph_cut_seam', 'multi_band', 'graph_cut_multi_band']:
                for levels in [1, 4]:
                    with self.subTest(shape=shape, mode=mode, levels=levels):
                        self.compare(colors, validity, edges, mode, levels)

    def test_full_mask_and_exactly_two_camera_corridor(self):
        h, w = 7, 11
        colors = np.zeros((h, w, 4, 3), np.float32)
        colors[..., 0, 0] = .7
        colors[..., 1, 1] = .4
        validity = np.zeros((h, w, 4), bool)
        validity[:, :8, 0] = True
        validity[:, 3:, 1] = True
        edges = np.ones((h, w, 4), np.float32)
        self.compare(colors, validity, edges, 'graph_cut_seam', smoothness=.7)
        validity[:] = True
        for mode in ['seam_distance_feather', 'graph_cut_seam', 'multi_band', 'graph_cut_multi_band']:
            self.compare(colors, validity, edges, mode)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
