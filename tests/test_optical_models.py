"""Analytic optical known answers and fixed-pose/metric-plane controls."""
from pathlib import Path
import sys
import unittest

import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from calibration.optical_models import FAMILIES, OpticalModel, metric_validation, project
from calibration.raster_board import K, image, poses


def estimate(factor=1.):
    return dict(fx=K[0,0]*factor,fy=K[1,1]*factor,cx=K[0,2],cy=K[1,2],
                k=[0.,0.,0.,0.],theta_max_rad=1.)


class OpticalTests(unittest.TestCase):
    def test_analytic_optical_radii(self):
        theta = np.pi/2
        self.assertAlmostEqual(OpticalModel('equisolid').radius(theta),np.sqrt(2))
        self.assertAlmostEqual(OpticalModel('stereographic').radius(theta),2.)
        self.assertAlmostEqual(OpticalModel('equidistant').radius(theta),theta)
        self.assertAlmostEqual(FAMILIES[1].radius(1.),1.138)

    def test_inverse_roundtrip_and_domain_rejection(self):
        theta = np.linspace(0,1.3,200)
        for family in FAMILIES:
            np.testing.assert_allclose(family.inverse(family.radius(theta)),theta,atol=2e-12)
        with self.assertRaises(ValueError):
            FAMILIES[1].inverse(np.array([np.nan]))
        with self.assertRaises(ValueError):
            OpticalModel('equisolid').inverse(np.array([2.1]))
        with self.assertRaises(ValueError):
            OpticalModel('kb_nonzero',(-1.,0.,0.,0.)).inverse(np.array([.1]))

    def test_inverse_respects_declared_domain(self):
        # Monotonic to theta=1, nonmonotonic outside it: do not extrapolate to pi/2.
        family = OpticalModel('kb_nonzero',(-.2,0.,0.,0.))
        np.testing.assert_allclose(family.inverse(family.radius(np.array([.2,.8])),theta_max=1.),[.2,.8],atol=1e-11)
        with self.assertRaises(ValueError):
            family.inverse(np.array([.1]))

    def test_optical_axis_projection(self):
        for family in FAMILIES:
            np.testing.assert_array_equal(project([[0.,0.,4.]],K,family),[[319.5,239.5]])

    def test_exact_estimate_has_zero_fixed_pose_and_floor_errors(self):
        result = metric_validation(estimate(),K,FAMILIES[0],poses(99)[12:])
        self.assertLess(result['fixed_pose_corner_px']['max'],1e-10)
        self.assertLess(result['floor_error_m']['max'],1e-9)
        self.assertEqual(result['invalid_floor_points'],0)
        for zone in result['ray_error_px'].values():
            self.assertLess(zone['max'],1e-10)

    def test_focal_error_cannot_be_hidden_by_refitting_pose(self):
        result = metric_validation(estimate(1.05),K,FAMILIES[0],poses(99)[12:])
        self.assertGreater(result['fixed_pose_corner_px']['rmse'],1.)
        self.assertGreater(result['floor_error_m']['p95'],.1)
        self.assertGreater(result['ray_error_px']['outer']['p95'],result['ray_error_px']['central']['p95'])
        # Hand derivation at floor X=0,Z=3,h=1.2: z'=h/tan(atan(h/z)/1.05).
        pixel = project([[0.,1.2,3.]],K,FAMILIES[0])[0]
        radius = np.linalg.norm((pixel-[K[0,2],K[1,2]])/[K[0,0]*1.05,K[1,1]*1.05])
        recovered_z = 1.2/np.tan(radius)
        self.assertAlmostEqual(recovered_z,1.2/np.tan(np.arctan(1.2/3.)/1.05))
        self.assertGreater(recovered_z,3.)

    def test_nonideal_families_change_real_raster_pixels(self):
        rotation,translation = poses(3101)[0]
        baseline = image(rotation,translation,optics=FAMILIES[0])
        for family in FAMILIES[1:]:
            self.assertGreater(np.count_nonzero(image(rotation,translation,optics=family)!=baseline),100)


if __name__ == '__main__':
    unittest.main()
