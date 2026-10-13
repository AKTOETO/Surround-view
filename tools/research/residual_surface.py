"""Residual source-surface audit: native samples, exported triangles, independent rays."""
import argparse
import json
from pathlib import Path
import numpy as np
from source_correspondence import sha, verified, load_product, project
from source_footprint_audit import footprint
from native_facing import collect as validate_native


def trace_triangles(origin, direction, triangles, kinds, distance=200.):
    """Two-sided nearest segment/triangle intersection; geometric point sampling only."""
    o,d,t = [np.asarray(v,float) for v in (origin,direction,triangles)]
    kinds=np.asarray(kinds)
    if o.shape!=(3,) or d.shape!=(3,) or t.ndim!=3 or t.shape[1:]!=(3,3) or kinds.shape!=(len(t),):
        raise ValueError('expected XYZ origin/direction and Nx3x3 triangles with N kinds')
    if not all(np.isfinite(v).all() for v in (o,d,t)) or not np.isfinite(distance) or distance<=0 or np.linalg.norm(d)==0:
        raise ValueError('finite geometry, nonzero direction and positive distance required')
    d=d/np.linalg.norm(d)
    e1,e2=t[:,1]-t[:,0],t[:,2]-t[:,0]
    h=np.cross(d,e2); det=np.einsum('ij,ij->i',e1,h)
    inv=np.divide(1.,det,out=np.zeros_like(det),where=np.abs(det)>1e-12)
    s=o-t[:,0];u=np.einsum('ij,ij->i',s,h)*inv
    q=np.cross(s,e1);v=q@d*inv;length=np.einsum('ij,ij->i',e2,q)*inv
    hit=(np.abs(det)>1e-12)&(u>=-1e-10)&(v>=-1e-10)&(u+v<=1+1e-10)&(length>=0)&(length<=distance)
    if not hit.any():return dict(kind=0,point=None,triangle=None,distance_m=None)
    i=int(np.argmin(np.where(hit,length,np.inf)))
    return dict(kind=int(kinds[i]),point=(o+d*length[i]).tolist(),triangle=i,distance_m=float(length[i]))


def inverse_ray(camera,uv,vehicle_pose):
    k=camera['projection']
    if any(k['k']) or k.get('alpha',0):raise ValueError('restricted zero-distortion equidistant optics required')
    a,b=(np.asarray(uv,float)-[k['cx'],k['cy']])/[k['fx'],k['fy']]
    theta=np.hypot(a,b);scale=np.sin(theta)/theta if theta>1e-14 else 1.
    pose=np.asarray(vehicle_pose)@np.linalg.inv(np.asarray(camera['T_camera_from_vehicle']))
    return pose[:3,3],pose[:3,:3]@np.array([a*scale,b*scale,np.cos(theta)])


def prepare(root,output):
    primary=json.loads((root/'docs/validation/baselines/native_facing_v1.json').read_text())
    if validate_native(root,root/'configs/research/native-facing-plan.json')!=primary:raise ValueError('native audit differs')
    residual=json.loads((root/'docs/validation/baselines/facing_entry_gap_v1.json').read_text())
    frames=[]
    for frame in [0,1]:
        coordinates=sorted({(p['y'],p['x']) for r in residual['rows'] if r['frame']==frame for p in r['reference_pairs']})
        ys,xs=np.array(coordinates).T
        cases=[];points=None;uvs=np.full((len(coordinates),4,2),np.nan)
        config=None
        for r in primary['rows']:
            if r['frame']!=frame or not r['facing_enabled']:continue
            directory=root/r['directory'];mp=verified(directory/'report.json',r['report_sha256']);m=json.loads(mp.read_text())
            p={v['file']:load_product(directory,v) for v in m['products']}
            xyz=p['carrier-positions.f32'][ys,xs]
            if points is not None and not np.array_equal(xyz,points):raise ValueError('selected carrier positions differ')
            points=xyz
            statuses=np.stack([p[f'camera{i}-status.u8'][ys,xs,0] for i in range(4)],axis=-1)
            for camera in range(4):
                valid=statuses[:,camera]==1
                actual=p[f'camera{camera}-pixels.f32'][ys,xs][valid]
                previous=uvs[valid,camera];have=np.isfinite(previous).all(axis=1)
                if not np.array_equal(previous[have],actual[have]):raise ValueError('retained UV differs across conditions')
                uvs[valid,camera]=actual
            cases.append(dict(variant=r['variant'],shift=r['shift'],kind=r['kind'],policy=r['policy'],
                directory=r['directory'],report_sha256=r['report_sha256'],statuses=statuses.tolist()))
            if config is not None and config['cameras']!=m['config']['cameras']:raise ValueError('camera calibration differs')
            config=m['config']
        if len(cases)!=18:raise ValueError('incomplete native frame')
        # Unobserved alternatives are independent projections, never called native samples.
        projection=[];validity=[]
        for camera in config['cameras']:
            uv,valid=project(camera,points)
            projection.append(uv.tolist());validity.append(valid.tolist())
        frames.append(dict(frame=frame,coordinates_yx=coordinates,carrier_xyz_vehicle_m=points.tolist(),config=config,
            native_uv=[[v.tolist() if np.isfinite(v).all() else None for v in row] for row in uvs],
            independent_uv=np.swapaxes(np.asarray(projection),0,1).tolist(),
            independent_valid=np.asarray(validity).T.tolist(),cases=cases))
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(dict(schema_version=1,frames=frames),indent=2,allow_nan=False)+'\n')


def collect(root,plan_path):
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    inputs=json.loads((root/plan['prepared_input']).read_text())
    truth_path=root/plan['output_root']/'scene-truth.json';truth=json.loads(truth_path.read_text())
    if truth['plan_sha256']!=sha(plan_path) or truth['helper_sha256']!=sha(root/'tools/blender/residual_surface_truth.py'):
        raise ValueError('scene truth binding differs')
    for frame in truth['frames']:
        for name,digest in frame['files'].items():verified(truth_path.parent/name,digest)
        if not frame['direct_target_mask_equal'] or not frame['target_visibility_equal']:raise ValueError('scene reconstruction differs')
    rows=[];comparisons=[]
    for data,scene in zip(inputs['frames'],truth['frames'],strict=True):
        if data['frame']!=scene['frame']:raise ValueError('frame order differs')
        triangles=np.load(truth_path.parent/scene['triangles'],allow_pickle=False)
        kinds=np.load(truth_path.parent/scene['triangle_kinds'],allow_pickle=False)
        for entry in scene['rays']:
            independent=trace_triangles(entry['origin'],entry['direction'],triangles,kinds,entry['max_distance_m'])
            native=entry['hit'];same=independent['kind']==native['kind']
            error=(float(np.linalg.norm(np.asarray(independent['point'])-native['point'])) if independent['point'] is not None and native['point'] is not None else None)
            comparisons.append(dict(frame=data['frame'],name=entry['name'],kind_equal=same,point_error_m=error,
                passed=same and (error is None or error<=plan['ray_oracle_tolerance_m'])))
        for index,coordinate in enumerate(data['coordinates_yx']):
            rays={r['name']:r for r in scene['rays']}
            direct=rays[f'virtual-{index}']['hit'];sources=[]
            for camera in range(4):
                center=rays.get(f'source-{index}-{camera}')
                if center is None:
                    sources.append(dict(camera=camera,projection_valid=False));continue
                hit=center['hit'];direct_point=direct['point']
                error=(float(np.linalg.norm(np.asarray(hit['point'])-direct_point)) if hit['point'] is not None and direct_point is not None else None)
                footprint_hits=[rays[f'texel-{index}-{camera}-{j}']['hit'] for j in range(4)]
                weights=center['bilinear_coefficients']
                footprint_target_weight=sum(w for w,h in zip(weights,footprint_hits) if h['kind']==1)
                sources.append(dict(camera=camera,projection_valid=True,uv_kind=center['uv_kind'],uv=center['uv'],
                    center_kind=hit['kind'],center_point_world_m=hit['point'],distance_to_virtual_hit_m=error,
                    matched={str(tol):bool(hit['kind']==direct['kind'] and hit['kind']!=0 and error<=tol) for tol in plan['surface_match_tolerances_m']},
                    center_to_carrier_m=float(np.linalg.norm(np.asarray(hit['point'])-scene['carrier_xyz_world_m'][index])) if hit['point'] is not None else None,
                    texel_kinds=[h['kind'] for h in footprint_hits],texel_target_coefficient=footprint_target_weight))
            cases=[]
            for case in data['cases']:
                statuses=case['statuses'][index]
                eligible=[s for s in sources if statuses[s['camera']]==1]
                cases.append(dict(variant=case['variant'],statuses=statuses,eligible_cameras=[s['camera'] for s in eligible],
                    matched_eligible={str(t):[s['camera'] for s in eligible if s['matched'][str(t)]] for t in plan['surface_match_tolerances_m']}))
            rows.append(dict(frame=data['frame'],y=coordinate[0],x=coordinate[1],virtual_kind=direct['kind'],virtual_point_world_m=direct['point'],
                sources=sources,cases=cases))
    return dict(schema_version=1,scope=plan['scope'],plan_sha256=sha(plan_path),collector_sha256=sha(__file__),truth_sha256=sha(truth_path),
        oracle_comparisons=comparisons,oracle_all_passed=all(r['passed'] for r in comparisons),rows=rows,limitations=plan['limitations'])


if __name__=='__main__':
    root=Path(__file__).resolve().parents[2]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare',type=Path)
    parser.add_argument('--plan',type=Path,default=Path('configs/research/residual-surface-plan.json'))
    parser.add_argument('--output-report',type=Path)
    args=parser.parse_args()
    if args.prepare:prepare(root,args.prepare)
    if args.output_report:
        args.output_report.write_text(json.dumps(collect(root,args.plan.resolve()),indent=2,allow_nan=False)+'\n')
