"""Cross-check the C++ projector against an independent NumPy ray-angle model."""
import copy,json,subprocess,sys,tempfile
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from simulator import project,camera_rays
binary,config_path=sys.argv[1:];cfg=json.loads(Path(config_path).read_text());rng=np.random.default_rng(420)
points=rng.uniform([-6,-4.5,0],[6,4.5,1.5],size=(1000,3))
errors=[];total=0
for coefficients in [[0.,0.,0.,0.],[.03,-.005,.001,.0001]]:
    case=copy.deepcopy(cfg);inputs=[];expected=[]
    for cam in case['cameras']:
        cam['projection']['k']=coefficients
        uv=project(cam,points);T=np.array(cam['T_camera_from_vehicle']);cp=points@T[:3,:3].T+T[:3,3]
        theta=np.arctan2(np.hypot(cp[:,0],cp[:,1]),cp[:,2]);r=cam['resolution'];k=cam['projection']
        valid=(cp[:,2]>k['z_epsilon_m'])&(theta<=k['theta_max_rad'])&(uv[:,0]>=0)&(uv[:,0]<=r['width']-1)&(uv[:,1]>=0)&(uv[:,1]<=r['height']-1)
        for p,pixel,ok in zip(points,uv,valid):inputs.append([cam['id'],*p]);expected.append((pixel,bool(ok)))
        # Check the inverse used for fixtures: unit rays and original pixels.
        identity=copy.deepcopy(cam);identity['T_camera_from_vehicle']=np.eye(4).tolist()
        rays,angles=camera_rays(identity)
        assert np.max(np.abs(np.linalg.norm(rays,axis=-1)-1))<1e-12,'non-unit simulator ray'
        xx,yy=np.meshgrid(np.arange(r['width']),np.arange(r['height']))
        mask=angles.ravel()<=k['theta_max_rad']
        recovered=project(identity,rays.reshape(-1,3)[mask])
        original=np.stack([xx,yy],axis=-1).reshape(-1,2)[mask]
        assert np.max(np.linalg.norm(recovered-original,axis=1))<1e-9,'inverse fisheye mismatch'
    with tempfile.TemporaryDirectory(prefix='sv-math-') as temporary:
        filename=Path(temporary)/'config.json';filename.write_text(json.dumps(case))
        proc=subprocess.run([binary,str(filename)],input=json.dumps(inputs),text=True,capture_output=True,check=True)
    actual=json.loads(proc.stdout)
    assert len(actual)==len(expected)==4000,'projector response count'
    for (pixel,valid),p in zip(expected,actual):
        assert p['valid']==valid,'independent validity mismatch'
        if valid:errors.append(np.linalg.norm(pixel-[p['u'],p['v']]))
    total+=len(actual)
assert max(errors)<1e-10
print(f'NumPy independent oracle: {total} points, zero/nonzero distortion, max error {max(errors):.3g} px')
