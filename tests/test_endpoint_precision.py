"""Analytic controls for independent high-precision face and host-float replay."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/research'))
from endpoint_precision import face_oracle, slab_replay

class EndpointPrecision(unittest.TestCase):
    def test_exact_strict_endpoint_bias(self):
        for bias,blocked in [(.125,True),(.25,False),(.5,False)]:
            r=face_oracle([0,0,0],[2,0,0],[1.75,-1,-1],[3,1,1],bias)
            self.assertEqual(r['blocked'],blocked)
            self.assertEqual(float(r['gap_m']),.25)
    def test_miss_parallel_and_inside(self):
        self.assertIsNone(face_oracle([0,2,0],[2,2,0],[1,-1,-1],[3,1,1],0)['gap_m'])
        r=face_oracle([2,0,0],[2.5,0,0],[1,-1,-1],[3,1,1],.25)
        self.assertEqual(r['face'],'origin_inside');self.assertTrue(r['blocked'])
    def test_axis_translation_and_orientation(self):
        shift=np.array([7,-3,2]);o=np.array([0,0,0]);p=np.array([2,0,0]);lo=np.array([1.75,-1,-1]);hi=np.array([3,1,1])
        r=face_oracle(o+shift,p+shift,lo+shift,hi+shift,.125)
        self.assertTrue(r['blocked']);self.assertEqual(float(r['gap_m']),.25)
        self.assertTrue(face_oracle(o[::-1],p[::-1],lo[::-1],hi[::-1],.125)['blocked'])
    def test_known_float_replays(self):
        for dtype in [False,True]:
            for bias,expected in [(.125,True),(.25,False),(.5,False)]:
                self.assertEqual(slab_replay([0,0,0],[2,0,0],[1.75,-1,-1],[3,1,1],0,bias,True,True,dtype)['blocked'],expected)
    def test_reject_invalid_geometry(self):
        for lo,hi,bias in [([2,0,0],[1,1,1],0),([0,0,0],[1,1,1],-1),([float('nan'),0,0],[1,1,1],0)]:
            with self.assertRaises(ValueError):face_oracle([0]*3,[2]*3,lo,hi,bias)

if __name__=='__main__':unittest.main()
