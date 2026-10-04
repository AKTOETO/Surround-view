import copy,json,sys,unittest
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from simulator import config,project
from configurator import calibrate,diagnose,rotation


def observation_fixture(noise=.08):
    cfg=config();rng=np.random.default_rng(20261005);cameras=[]
    for cam in cfg['cameras']:
        T=np.array(cam['T_camera_from_vehicle']);R=T[:3,:3];origin=-R.T@T[:3,3]
        data={}
        for split in ['train','validation']:
            # Known 3D points in multiple depths; final verification points are never fitted.
            theta=rng.uniform(.05,1.25,180);phi=rng.uniform(-np.pi,np.pi,180);d=rng.uniform(2,7,180)
            rays=np.stack([np.sin(theta)*np.cos(phi),np.sin(theta)*np.sin(phi),np.cos(theta)],axis=1)
            points=origin+(rays*d[:,None])@R
            uv=project(cam,points);r=cam['resolution'];mask=(uv[:,0]>=0)&(uv[:,0]<r['width']-1)&(uv[:,1]>=0)&(uv[:,1]<r['height']-1)
            points=points[mask];pixels=uv[mask]+rng.normal(0,noise,(sum(mask),2))
            data[split]=dict(points=points.tolist(),pixels=pixels.tolist())
        cameras.append(dict(id=cam['id'],**data))
    return dict(schema_version=1,origin='independent_analytic_correspondences',cameras=cameras)


def perturbed_config():
    cfg=config()
    for cam in cfg['cameras']:
        cam['projection']['fx']*=1.02;cam['projection']['fy']*=.985
        cam['projection']['cx']+=1.;cam['projection']['cy']-=.7
        T=np.array(cam['T_camera_from_vehicle']);T[:3,:3]=rotation([.006,-.004,.005])@T[:3,:3];T[:3,3]+=[.01,-.008,.015];cam['T_camera_from_vehicle']=T.tolist()
    return cfg


class CalibrationTests(unittest.TestCase):
    def test_recovery_and_drift(self):
        obs=observation_fixture();original=perturbed_config();result,report=calibrate(original,obs)
        self.assertTrue(report['accepted'],report)
        for row in report['cameras']:
            self.assertLess(row['validation']['p95_px'],.5)
            self.assertLess(row['validation']['rmse_px'],row['before_validation']['rmse_px']/3)
        self.assertEqual(diagnose(result,obs)['state'],'NORMAL')
        bad=copy.deepcopy(result);T=np.array(bad['cameras'][2]['T_camera_from_vehicle']);T[:3,:3]=rotation([0,.025,0])@T[:3,:3];bad['cameras'][2]['T_camera_from_vehicle']=T.tolist()
        self.assertEqual(diagnose(bad,obs)['candidates'],[2])
    def test_insufficient_points(self):
        obs=observation_fixture();obs['cameras'][0]['validation']=dict(points=[],pixels=[])
        self.assertEqual(diagnose(config(),obs)['state'],'INDETERMINATE')
        with self.assertRaises(ValueError):calibrate(config(),obs)
    def test_leakage(self):
        obs=observation_fixture();obs['cameras'][0]['validation']=copy.deepcopy(obs['cameras'][0]['train'])
        with self.assertRaisesRegex(ValueError,'overlap'):calibrate(config(),obs)
    def test_invalid_camera_set(self):
        obs=observation_fixture()
        for malformed in [dict(schema_version=2,cameras=obs['cameras']),dict(schema_version=1,cameras=obs['cameras'][:3]),dict(schema_version=1,cameras=[])]:
            with self.subTest(count=len(malformed['cameras']),version=malformed['schema_version']):
                with self.assertRaises(ValueError):diagnose(config(),malformed)
                with self.assertRaises(ValueError):calibrate(config(),malformed)
        obs['cameras'][1]['id']=0
        with self.assertRaises(ValueError):diagnose(config(),obs)
    def test_invalid_threshold(self):
        obs=observation_fixture()
        for value in [0.,-1.,float('nan'),float('inf')]:
            with self.subTest(value=value):
                with self.assertRaises(ValueError):diagnose(config(),obs,value)
                with self.assertRaises(ValueError):calibrate(config(),obs,value)
        with self.assertRaises(ValueError):diagnose(config(),obs,.2,.5)


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
