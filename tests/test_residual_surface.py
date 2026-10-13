"""Independent analytic controls for two-sided finite triangle rays."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/research'))
from residual_surface import trace_triangles,inverse_ray


class Triangles(unittest.TestCase):
    def setUp(self):
        self.triangles=np.array([[[-1,-1,2],[1,-1,2],[0,1,2]],[[-1,-1,4],[1,-1,4],[0,1,4]]],float)

    def test_nearest_and_two_sided(self):
        h=trace_triangles([0,0,0],[0,0,3],self.triangles,[1,2])
        self.assertEqual(h['kind'],1);np.testing.assert_allclose(h['point'],[0,0,2])
        self.assertEqual(trace_triangles([0,0,5],[0,0,-1],self.triangles,[1,2])['kind'],2)

    def test_miss_parallel_and_segment_limit(self):
        for origin,direction,distance in [([3,0,0],[0,0,1],10),([0,0,0],[1,0,0],10),([0,0,0],[0,0,1],1.99)]:
            self.assertEqual(trace_triangles(origin,direction,self.triangles,[1,2],distance)['kind'],0)
        self.assertEqual(trace_triangles([0,0,0],[0,0,1],self.triangles,[1,2],2)['kind'],1)

    def test_vertex_and_degenerate_triangle(self):
        triangles=np.concatenate([np.zeros((1,3,3)),self.triangles])
        self.assertEqual(trace_triangles([-1,-1,0],[0,0,1],triangles,[9,1,2])['kind'],1)

    def test_triangle_order_winding_translation(self):
        shift=np.array([7,-2,11]);tri=self.triangles[::-1,::-1]+shift
        h=trace_triangles(shift,[0,0,1],tri,[2,1])
        self.assertEqual(h['kind'],1);np.testing.assert_allclose(h['point'],shift+[0,0,2])

    def test_invalid_and_empty(self):
        self.assertEqual(trace_triangles([0]*3,[0,0,1],np.empty((0,3,3)),[])['kind'],0)
        for o,d in [([0,0],[0,0,1]),([0]*3,[0]*3),([np.nan,0,0],[0,0,1])]:
            with self.assertRaises(ValueError):trace_triangles(o,d,self.triangles,[1,2])

    def test_known_equidistant_inverse(self):
        camera={'projection':dict(k=[0]*4,fx=100,fy=100,cx=200,cy=200),'T_camera_from_vehicle':np.eye(4).tolist()}
        o,d=inverse_ray(camera,[200+100*np.pi/4,200],np.eye(4))
        np.testing.assert_allclose(o,[0]*3);np.testing.assert_allclose(d,[2**-.5,0,2**-.5])


if __name__=='__main__':unittest.main()
