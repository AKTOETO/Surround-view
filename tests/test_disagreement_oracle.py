"""Known-answer controls for the independent audit sampler, not product implementation."""
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/research'))
from source_disagreement import bilinear_rgb

class Sampler(unittest.TestCase):
    def test_pixel_centres_and_asymmetric_weights(self):
        image=np.array([[[0,10,20],[100,110,120]],[[200,210,220],[240,250,255]]],np.uint8)
        np.testing.assert_array_equal(bilinear_rgb(image,[1,0]),[100,110,120])
        expected=.375*image[0,0]+.125*image[0,1]+.375*image[1,0]+.125*image[1,1]
        np.testing.assert_allclose(bilinear_rgb(image,[.25,.5]),expected)
    def test_clamp_and_constant_black(self):
        image=np.zeros((2,3,3),np.uint8);image[0,0]=[4,5,6]
        np.testing.assert_array_equal(bilinear_rgb(image,[-.25,-.5]),[4,5,6])
        np.testing.assert_array_equal(bilinear_rgb(image,[2.5,1.5]),[0,0,0])
    def test_invalid_shape_or_coordinate(self):
        for image,uv in [(np.zeros((2,2)),[0,0]),(np.zeros((0,2,3)),[0,0]),(np.zeros((2,2,3)),[float('nan'),0]),(np.zeros((2,2,3)),[0])]:
            with self.assertRaises(ValueError):bilinear_rgb(image,uv)

if __name__=='__main__':unittest.main()
