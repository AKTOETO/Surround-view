"""Known raster geometry, production image detector, and explicit blank-image rejection."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from calibration.raster_board import K, SIZE, corners, detection_error, image, poses

BINARY = sys.argv[1] if len(sys.argv) > 1 else str(ROOT/'build/sv-calibrate')
sys.argv = sys.argv[:1]


class RasterCalibrationTests(unittest.TestCase):
    def test_axis_and_grid_known_answer(self):
        expected = corners(np.eye(3),np.array([0.,0.,1.]))
        # First corner is (-.32,-.20,1); independent scalar atan projection.
        radius = np.hypot(.32,.20)
        np.testing.assert_allclose(expected[0],
            [319.5-300*.32*np.arctan(radius)/radius,239.5-295*.20*np.arctan(radius)/radius])
        np.testing.assert_allclose(expected[0]+expected[-1],[639.,479.])
        self.assertEqual(detection_error(expected[::-1],expected)['rmse_px'],0.)
        self.assertTrue(detection_error(expected[::-1],expected)['reversed_indexing'])

    def test_raster_is_deterministic_and_board_alternates(self):
        rotation,translation = np.eye(3),np.array([0.,0.,1.])
        clean = image(rotation,translation)
        self.assertEqual(clean.shape,SIZE[::-1])
        self.assertEqual(clean.dtype,np.uint8)
        self.assertGreater(int(clean.max())-int(clean.min()),200)
        # Adjacent cells on either side of central corner have opposing brightness.
        self.assertGreater(abs(int(clean[242,321])-int(clean[242,316])),150)
        np.testing.assert_array_equal(image(rotation,translation,1.5,.02,7),
                                      image(rotation,translation,1.5,.02,7))
        self.assertFalse(np.array_equal(image(rotation,translation,1.5,.02,7),
                                        image(rotation,translation,1.5,.02,8)))
        with self.assertRaises(ValueError):
            image(rotation,translation,noise=-1.)

    def test_seed_split_poses_are_distinct(self):
        a,b = poses(2401),poses(2402)
        self.assertEqual(len(a),16)
        self.assertEqual(len({tuple(t) for _,t in a}),16)
        self.assertFalse(np.array_equal(a[0][1],b[0][1]))

    def test_production_detector_and_blank_rejection(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            rotation,translation = poses(2401)[0]
            Image.fromarray(image(rotation,translation)).save(folder/'board.png')
            Image.fromarray(np.full(SIZE[::-1],192,np.uint8)).save(folder/'blank.png')
            for kind,success in [('board',True),('blank',False)]:
                process = subprocess.run([BINARY,'detect','--image',str(folder/f'{kind}.png'),
                                          '--output',str(folder/kind)],capture_output=True,text=True,timeout=30)
                self.assertEqual(process.returncode == 0,success,process.stderr)
                if success:
                    detected = json.loads((folder/kind/'detections.json').read_text())
                    error = detection_error([p['uv_px'] for p in detected['points']],corners(rotation,translation))
                    self.assertLess(error['rmse_px'],.5)
                else:
                    self.assertFalse((folder/kind/'detections.json').exists())
                    self.assertIn('not detected',process.stderr)


if __name__ == '__main__':
    unittest.main()
