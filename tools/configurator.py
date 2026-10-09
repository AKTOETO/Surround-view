#!/usr/bin/env python3
"""Offline known-point calibration. No image corner detector or live auto-calibration."""
import argparse, copy, hashlib, json, os, sys
from pathlib import Path
import numpy as np


def rotation(w):
    theta=np.linalg.norm(w); W=np.array([[0,-w[2],w[1]],[w[2],0,-w[0]],[-w[1],w[0],0]])
    if theta<1e-10:return np.eye(3)+W+.5*W@W
    return np.eye(3)+np.sin(theta)/theta*W+(1-np.cos(theta))/theta**2*W@W


def parameters(cam):
    p=cam['projection'];r=cam['resolution'];T=np.asarray(cam['T_camera_from_vehicle'])
    return np.r_[np.log([p['fx'],p['fy']]),p['cx']/r['width'],p['cy']/r['height'],p['k'],np.zeros(3),T[:3,3]/5.]


def predict(beta, points, cam):
    T=np.asarray(cam['T_camera_from_vehicle']);r=cam['resolution']
    R=rotation(beta[8:11])@T[:3,:3];p=np.asarray(points)@R.T+beta[11:14]*5.
    rho=np.hypot(p[:,0],p[:,1]);theta=np.arctan2(rho,p[:,2]);td=theta.copy()
    for j in range(4):td+=beta[4+j]*theta**(2*j+3)
    unit=np.divide(p[:,:2],rho[:,None],out=np.zeros_like(p[:,:2]),where=rho[:,None]>1e-12)
    return unit*td[:,None]*np.exp(beta[:2])+beta[2:4]*[r['width'],r['height']]


def unpack(beta,cam):
    result=copy.deepcopy(cam);p=result['projection'];r=result['resolution'];T=np.asarray(cam['T_camera_from_vehicle']).copy()
    T[:3,:3]=rotation(beta[8:11])@T[:3,:3];T[:3,3]=beta[11:14]*5
    p.update(fx=float(np.exp(beta[0])),fy=float(np.exp(beta[1])),cx=float(beta[2]*r['width']),cy=float(beta[3]*r['height']),k=beta[4:8].tolist())
    result['T_camera_from_vehicle']=T.tolist();return result


def validate_observations(data):
    points=np.asarray(data['points'],dtype=float);pixels=np.asarray(data['pixels'],dtype=float)
    if points.ndim!=2 or points.shape[1]!=3 or pixels.shape!=(len(points),2) or len(points)<30:
        raise ValueError('need >=30 known xyz/pixel correspondences per split')
    if not np.isfinite(points).all() or not np.isfinite(pixels).all():raise ValueError('nonfinite observation')
    if np.linalg.matrix_rank(points-points.mean(axis=0))<3:raise ValueError('known-point prototype requires noncoplanar aggregate observations')
    return points,pixels


def jacobian(beta,points,cam):
    eps=1e-5;columns=[]
    for j in range(len(beta)):
        left=beta.copy();right=beta.copy();left[j]-=eps;right[j]+=eps
        columns.append(((predict(right,points,cam)-predict(left,points,cam))/(2*eps)).ravel())
    return np.asarray(columns).T


def errors(cam,data):
    points,pixels=validate_observations(data);e=np.linalg.norm(predict(parameters(cam),points,cam)-pixels,axis=1)
    return dict(count=len(e),rmse_px=float(np.sqrt(np.mean(e*e))),p50_px=float(np.quantile(e,.5)),p95_px=float(np.quantile(e,.95)),max_px=float(e.max()))


def validate_camera_set(cfg, observations):
    if observations.get('schema_version')!=1 or cfg.get('schema_version')!=1:
        raise ValueError('observation/configuration schema version')
    cameras=cfg.get('cameras');data=observations.get('cameras')
    if not isinstance(cameras,list) or not isinstance(data,list) or len(cameras)!=4 or len(data)!=4:
        raise ValueError('need exactly four cameras and observation records')
    if [c.get('id') for c in cameras]!=list(range(4)) or [c.get('id') for c in data]!=list(range(4)):
        raise ValueError('camera ordering mismatch')


def validate_threshold(value):
    if not np.isfinite(value) or value<=0:raise ValueError('threshold must be finite and positive')


def fit(cam,data,iterations=100):
    points,pixels=validate_observations(data);beta=parameters(cam);damping=1e-3
    for iteration in range(iterations):
        residual=(predict(beta,points,cam)-pixels).ravel();J=jacobian(beta,points,cam)
        magnitude=np.linalg.norm(residual.reshape(-1,2),axis=1)
        weights=np.repeat(np.sqrt(np.minimum(1.,2./np.maximum(magnitude,1e-9))),2)
        A=J*weights[:,None];b=residual*weights;normal=A.T@A
        step=np.linalg.solve(normal+damping*np.diag(np.maximum(np.diag(normal),1.)), -A.T@b)
        candidate=beta+step
        if not np.isfinite(candidate).all() or np.max(np.abs(candidate[:2]))>12: damping*=10;continue
        next_residual=(predict(candidate,points,cam)-pixels).ravel()
        # Huber objective, with two-pixel transition, used for step acceptance too.
        def cost(r):
            e=np.linalg.norm(r.reshape(-1,2),axis=1);return np.sum(np.where(e<=2,.5*e*e,2*(e-1)))
        if cost(next_residual)<cost(residual):
            beta=candidate;damping=max(damping/3,1e-10)
            if np.linalg.norm(step)<1e-9:break
        else:damping=min(damping*10,1e12)
    result=unpack(beta,cam);J=jacobian(beta,points,cam);scaled=J/np.maximum(np.linalg.norm(J,axis=0),1e-12);singular=np.linalg.svd(scaled,compute_uv=False)
    info=dict(iterations=iteration+1,normalized_jacobian_condition=float(singular[0]/singular[-1]),rank=int(np.linalg.matrix_rank(scaled)),train=errors(result,data))
    return result,info


def diagnose(cfg, observations, threshold_px=1., suspect_px=.5):
    validate_camera_set(cfg,observations);validate_threshold(threshold_px);validate_threshold(suspect_px)
    if suspect_px>threshold_px:raise ValueError('suspect threshold exceeds recalibration threshold')
    reports=[];candidates=[];indeterminate=False
    for cam,data in zip(cfg['cameras'],observations['cameras']):
        if cam['id']!=data['id']:raise ValueError('camera ordering mismatch')
        try:
            e=errors(cam,data['validation']);state='RECALIBRATION_REQUIRED' if e['p95_px']>threshold_px else 'SUSPECT' if e['p95_px']>suspect_px else 'NORMAL'
            if state=='RECALIBRATION_REQUIRED':candidates.append(cam['id'])
            reports.append(dict(camera_id=cam['id'],state=state,**e))
        except (KeyError,ValueError):
            indeterminate=True;reports.append(dict(camera_id=cam['id'],state='INDETERMINATE',reason='insufficient_known_points'))
    return dict(method='known_vehicle_points_v1',threshold_px=threshold_px,suspect_px=suspect_px,candidates=candidates,
        state='RECALIBRATION_REQUIRED' if candidates else 'INDETERMINATE' if indeterminate else 'SUSPECT' if any(r['state']=='SUSPECT' for r in reports) else 'NORMAL',cameras=reports)


def calibrate(cfg, observations, max_validation_px=1.):
    validate_camera_set(cfg,observations);validate_threshold(max_validation_px)
    result=copy.deepcopy(cfg);reports=[]
    for index,(cam,data) in enumerate(zip(cfg['cameras'],observations['cameras'])):
        if cam['id']!=data['id']:raise ValueError('camera ordering mismatch')
        train_points,_=validate_observations(data['train']);val_points,_=validate_observations(data['validation'])
        train_keys={tuple(p) for p in train_points}
        if any(tuple(p) in train_keys for p in val_points):raise ValueError('train/validation known-point overlap')
        recovered,info=fit(cam,data['train']);info.update(camera_id=cam['id'],before_validation=errors(cam,data['validation']),validation=errors(recovered,data['validation']))
        k=np.asarray(recovered['projection']['k']);end=recovered['projection']['theta_max_rad']**2
        critical=np.roots(np.trim_zeros(np.array([36*k[3],21*k[2],10*k[1],3*k[0]]),'f')) if np.any(k) else []
        probes=[0.,end]+[float(t.real) for t in critical if abs(t.imag)<1e-8 and 0<t.real<end]
        monotonic=min(1+3*k[0]*t+5*k[1]*t*t+7*k[2]*t**3+9*k[3]*t**4 for t in probes)>1e-8
        info['accepted']=bool(info['rank']==14 and info['validation']['p95_px']<=max_validation_px and monotonic)
        result['cameras'][index]=recovered;reports.append(info)
    accepted=all(r['accepted'] for r in reports)
    digest=hashlib.sha256(json.dumps(observations,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    for cam in result['cameras']:cam['calibration_id']=f"known-points-{digest[:12]}-{cam['id']}"
    report=dict(method='known_vehicle_points_lm_v1',accepted=accepted,observation_sha256=digest,max_validation_p95_px=max_validation_px,cameras=reports,
        limitations=['Known correspondences only; no image detector','No joint unknown-template pose estimation','Synthetic observations are not real-camera validation'])
    return result,report


if __name__=='__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'compare-optics':
        import runpy
        sys.argv.pop(1)
        runpy.run_module('calibration.optics_study', run_name='__main__')
        raise SystemExit(0)
    if len(sys.argv) > 1 and sys.argv[1] == 'compare-mounts':
        from calibration.study import main
        main(sys.argv[2:])
        raise SystemExit(0)
    if len(sys.argv) > 1 and sys.argv[1] == 'calibrate-images':
        from calibration.study import board_main
        board_main(sys.argv[2:])
        raise SystemExit(0)
    if len(sys.argv) > 1 and sys.argv[1] == 'calibrate-image-survey':
        from calibration.study import board_survey_main
        board_survey_main(sys.argv[2:])
        raise SystemExit(0)
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['calibrate','diagnose']);parser.add_argument('--config',type=Path,required=True);parser.add_argument('--observations',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--threshold',type=float,default=1.)
    args=parser.parse_args();cfg=json.loads(args.config.read_text());obs=json.loads(args.observations.read_text());args.output.mkdir(parents=True,exist_ok=True)
    if args.mode=='diagnose':report=diagnose(cfg,obs,args.threshold)
    else:
        result,report=calibrate(cfg,obs,args.threshold)
        if report['accepted']:
            # Never overwrite an existing accepted configuration.
            dest=args.output/'calibrated.json'
            if dest.exists():raise SystemExit('output calibration already exists; choose a new output directory')
            temporary=args.output/'calibrated.json.tmp';temporary.write_text(json.dumps(result,indent=2)+'\n');os.replace(temporary,dest)
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));raise SystemExit(0 if report.get('accepted',True) else 2)
