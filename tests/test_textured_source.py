"""Physical camera identity must not silently absorb calibration error."""
import copy
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/research'))
from textured_source import calibration_delta


class PhysicalCalibration(unittest.TestCase):
    def setUp(self):
        self.camera = [dict(id=0, projection=dict(fx=100), T_camera_from_vehicle=np.eye(4).tolist())]

    def test_decimal_roundtrip_only(self):
        actual = copy.deepcopy(self.camera)
        actual[0]['T_camera_from_vehicle'][0][0] += 2e-16
        self.assertLessEqual(calibration_delta(actual, self.camera), 1e-12)

    def test_real_extrinsic_or_intrinsic_change_rejected(self):
        for extrinsic in [True, False]:
            actual = copy.deepcopy(self.camera)
            if extrinsic:
                actual[0]['T_camera_from_vehicle'][0][3] = 1e-7
            else:
                actual[0]['projection']['fx'] += 1e-10
            with self.assertRaises(ValueError):
                calibration_delta(actual, self.camera)

    def test_missing_camera_and_nonfinite_rejected(self):
        with self.assertRaises(ValueError):
            calibration_delta([], self.camera)
        actual = copy.deepcopy(self.camera)
        actual[0]['T_camera_from_vehicle'][0][3] = float('nan')
        with self.assertRaises(ValueError):
            calibration_delta(actual, self.camera)


if __name__ == '__main__':
    unittest.main()
