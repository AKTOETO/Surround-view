"""Known-answer controls for matched source truth, not a product renderer."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/research'))
from mesa_source_truth import virtual_segment,matching
from residual_surface import trace_triangles

class MatchedTruth(unittest.TestCase):
    def test_virtual_pixel_centre_and_finite_clipping(self):
        cfg=dict(virtual_camera=dict(azimuth_rad=0,elevation_rad=0,distance_m=10,fov_y_rad=1,clip_m=[1,20]),output=dict(width=2,height=2))
        o,d,length=virtual_segment(cfg,[.5,.5],np.eye(4))
        np.testing.assert_allclose(o,[9,0,0]);np.testing.assert_allclose(d,[-1,0,0]);self.assertAlmostEqual(length,19)
        tri=np.array([[[0,-1,-1],[0,1,-1],[0,0,1]]],float)
        hit=trace_triangles(o,d,tri,[2],length)
        self.assertEqual(hit['kind'],2);np.testing.assert_allclose(hit['point'],[0,0,0])
    def test_world_pose_translation(self):
        cfg=dict(virtual_camera=dict(azimuth_rad=0,elevation_rad=0,distance_m=10,fov_y_rad=1,clip_m=[1,20]),output=dict(width=2,height=2))
        pose=np.eye(4);pose[:3,3]=[3,4,5]
        o,d,_=virtual_segment(cfg,[.5,.5],pose)
        np.testing.assert_allclose(o,[12,4,5]);np.testing.assert_allclose(d,[-1,0,0])
    def test_missing_hit_and_wrong_object_not_matching(self):
        direct=dict(kind=2,point=[0,0,0])
        e,m=matching(dict(kind=0,point=None),direct,[.002,.02])
        self.assertIsNone(e);self.assertFalse(any(m.values()))
        _,m=matching(dict(kind=1,point=[0,0,0]),direct,[.002,.02]);self.assertFalse(any(m.values()))
    def test_point_tolerance_and_single_source_geometry(self):
        e,m=matching(dict(kind=2,point=[.01,0,0]),dict(kind=2,point=[0,0,0]),[.002,.02])
        self.assertAlmostEqual(e,.01);self.assertFalse(m['0.002']);self.assertTrue(m['0.02'])

if __name__=='__main__':unittest.main()
