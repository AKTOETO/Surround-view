"""Optics/coordinate/file contract tests. No Blender installation is required."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'blender'))
from rig import configuration, cube_coordinates, vehicle_pose
from convert import bilinear, convert, convert_camera, linear_rgb, srgb8


class BlenderFixtureTests(unittest.TestCase):
    def test_mounts_and_frame_conventions(self):
        centers = []
        for cam in configuration()['cameras']:
            transform = np.array(cam['T_camera_from_vehicle'])
            rotation = transform[:3,:3]
            np.testing.assert_allclose(rotation @ rotation.T,np.eye(3),atol=1e-14)
            self.assertAlmostEqual(np.linalg.det(rotation),1.)
            center = -rotation.T @ transform[:3,3]
            centers.append(center)
            np.testing.assert_allclose(transform @ [*center,1],[0,0,0,1],atol=1e-14)
            self.assertLess(rotation[2,2],0)  # Optical axis points slightly down.
        self.assertGreater(centers[0][0],2.3)
        self.assertLess(centers[2][0],-2.3)
        # Mirror is centered at |y|=.981 and has half-width .08 m.
        self.assertGreater(abs(centers[1][1]),1.061+.025)
        self.assertGreater(abs(centers[3][1]),1.061+.025)
        np.testing.assert_allclose(vehicle_pose(30)[:3,3],[2,0,0])

    def test_face_centers_and_image_axes(self):
        rays = np.array([[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1]])
        mappings = cube_coordinates(rays,64)
        for index,name in enumerate(('px','nx','py','ny','pz','nz')):
            mask,uv = mappings[name]
            self.assertEqual(np.flatnonzero(mask).tolist(),[index])
            np.testing.assert_allclose(uv[index],[31.5,31.5])
        # Optical +X must point right, +Y down on forward face, including corners.
        mask, uv = cube_coordinates(np.array([[.5,.25,1],[-.5,-.25,1]]),64)['pz']
        self.assertTrue(mask.all())
        np.testing.assert_allclose(uv,[[47.5,39.5],[15.5,23.5]])

    def test_direction_field_roundtrip(self):
        size = 128
        yy,xx = np.mgrid[:size,:size]
        a,b = (xx+.5)*2/size-1,(yy+.5)*2/size-1
        one = np.ones_like(a)
        # Independent face equations, not the producer's face_basis implementation.
        directions = {'px':(one,b,-a), 'nx':(-one,b,a), 'py':(a,one,-b),
                      'ny':(a,-one,b), 'pz':(a,b,one), 'nz':(-a,b,-one)}
        faces = {}
        for name, components in directions.items():
            d = np.stack(components,axis=-1)
            d /= np.linalg.norm(d,axis=-1,keepdims=True)
            faces[name] = srgb8((d+1)/2)
        cam = configuration()['cameras'][0]
        result = linear_rgb(convert_camera(cam,faces,size))
        # Closed-form equidistant inverse, independent of camera_rays Newton solver.
        yy,xx = np.mgrid[:400,:400]
        a,b = (xx-199.5)/128,(yy-199.5)/128
        theta = np.hypot(a,b)
        ratio = np.sinc(theta/np.pi)
        expected = (np.stack([a*ratio,b*ratio,np.cos(theta)],axis=-1)+1)/2
        valid = theta <= 1.48
        self.assertLess(np.max(np.abs(result[valid]-expected[valid])),.008)
        self.assertTrue((result[~valid] == 0).all())
        # Missing negative-Z face is fine for <180 degree cameras; missing +Z isn't.
        faces.pop('nz')
        convert_camera(cam,faces,size)
        faces.pop('pz')
        with self.assertRaises(ValueError):
            convert_camera(cam,faces,size)

    def test_bilinear_and_linear_light(self):
        pixels = np.array([[[0,0,0],[255,255,255]]],np.uint8)
        value = srgb8(bilinear(linear_rgb(pixels),np.array([[.5,0]])))
        np.testing.assert_array_equal(value,[[188,188,188]])

    def test_manifest_hashes_and_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'capture'
            source.mkdir()
            image = np.full((32,32,3),170,np.uint8)
            Image.fromarray(image).save(source/'face.png')
            digest = hashlib.sha256((source/'face.png').read_bytes()).hexdigest()
            cfg = configuration()
            captures = [{'id':c['id'],'faces':dict.fromkeys(('px','nx','py','ny','pz'),'face.png'),
                         'T_world_from_camera':np.linalg.inv(c['T_camera_from_vehicle']).tolist()}
                        for c in cfg['cameras']]
            metadata = {'schema_version':1,'origin':'test','blender_version':'test','engine':'test',
                        'view_transform':'Standard','fps':30,'script_sha256':{},'limitations':[],
                        'config':cfg,'face_size':32,'sha256':{'face.png':digest},
                        'frames':[{'scenario_timestamp_ns':'0','T_world_from_vehicle':np.eye(4).tolist(),
                                   'cameras':captures}]}
            (source/'capture.json').write_text(json.dumps(metadata))
            output = Path(temporary)/'replay'
            manifest = json.loads(convert(source,output).read_text())
            self.assertEqual(len(manifest['sha256']),4)
            self.assertEqual(manifest['calibration_ids'],[c['calibration_id'] for c in cfg['cameras']])
            for path,expected in manifest['sha256'].items():
                self.assertEqual(hashlib.sha256((output/path).read_bytes()).hexdigest(),expected)
            with self.assertRaises(FileExistsError):
                convert(source,output)
            (source/'face.png').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError,'checksum'):
                convert(source,Path(temporary)/'bad')
            self.assertFalse((Path(temporary)/'bad').exists())

    def test_unsafe_capture_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'capture.json').write_text(json.dumps({
                'schema_version':1,'frames':[{}],'sha256':{'../escape':'unused'}}))
            with self.assertRaisesRegex(ValueError,'unsafe'):
                convert(root,root/'output')


if __name__ == '__main__':
    unittest.main()
