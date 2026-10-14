import sys
from pathlib import Path
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/blender'))
from textured_fixture import texture


class TextureTests(unittest.TestCase):
    def test_seed_repeatability_and_independence(self):
        self.assertTrue(np.array_equal(texture(211), texture(211)))
        self.assertFalse(np.array_equal(texture(211), texture(223)))

    def test_nonuniform_rgb_and_bounds(self):
        image = texture(211)
        self.assertEqual(image.shape, (128, 128, 3))
        self.assertEqual(image.dtype, np.uint8)
        self.assertGreater(image.min(), 0)
        self.assertLess(image.max(), 255)
        self.assertTrue((image.std(axis=(0, 1)) > 30).all())

    def test_invalid_parameters(self):
        for seed, size in [(True, 128), (-1, 128), (2**32, 128), (0, 8), (0, True)]:
            with self.assertRaises(ValueError):
                texture(seed, size)


if __name__ == '__main__':
    unittest.main()
