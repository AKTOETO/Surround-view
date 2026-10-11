"""Read-only geometric planar control, with an independent analytical plane oracle."""
import json
from pathlib import Path
import numpy as np

from source_correspondence import sha, verified, load_product, project, describe
from source_correspondence_controls import transform_points


def intersect_plane(origins, directions):
    origins, directions = np.broadcast_arrays(np.asarray(origins,float), np.asarray(directions,float))
    if origins.shape[-1] != 3 or not np.isfinite(origins).all() or not np.isfinite(directions).all():
        raise ValueError('finite 3D rays required')
    dz = directions[...,2]
    if np.any(np.abs(dz)<1e-12):
        raise ValueError('parallel ray')
    distance = -origins[...,2]/dz
    if np.any(distance<=0):
        raise ValueError('plane behind ray')
    return origins + distance[...,None]*directions


def virtual_rays(config, pixels, ego):
    # Independent look-at construction, not imported from Blender's virtual_pose.
    view = config['virtual_camera']
    target = np.asarray(view.get('target_m',[0,0,0]),float)
    a,e = view['azimuth_rad'],view['elevation_rad']
    eye = target+view['distance_m']*np.array([np.cos(e)*np.cos(a),np.cos(e)*np.sin(a),np.sin(e)])
    forward = target-eye;forward/=np.linalg.norm(forward)
    right = np.cross(forward,[0,0,1.]);right/=np.linalg.norm(right)
    up = np.cross(right,forward)
    h,w = config['output']['height'],config['output']['width']
    y,x = pixels.T
    tangent = np.tan(view['fov_y_rad']/2)
    local = np.column_stack([(2*(x+.5)/w-1)*tangent*w/h,(1-2*(y+.5)/h)*tangent,-np.ones(len(x))])
    rotation = np.column_stack([right,up,-forward])
    return transform_points(ego,eye), local@rotation.T@ego[:3,:3].T


def collect(root, plan_path):
    plan = json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():
        verified(root/name,digest)
    output = root/plan['output_root']
    scene_path = output/'scene-truth.json'
    scene = json.loads(scene_path.read_text())
    if scene['plan_sha256']!=sha(plan_path) or scene['plane_z_m']!=0 or scene['scene_mesh_count']!=1:
        raise ValueError('plane truth differs from plan')
    verified(root/'tools/blender/planar_correspondence.py',scene['helper_sha256'])
    if [r['frame_index'] for r in scene['records']] != plan['frames']:
        raise ValueError('frame order differs')
    archived = json.loads((root/plan['truth_path']).read_text())
    rows=[]
    for record in scene['records']:
        frame=record['frame_index'];directory=output/f'frame{frame}'
        arrays={n:np.load(verified(directory/n,h),allow_pickle=False) for n,h in record['files'].items()}
        pixels=arrays['pixels.npy'];y,x=pixels.T
        source=root/plan['native_root']/f'frame{frame}'
        if not np.array_equal(pixels,np.argwhere(np.load(source/'direct-mask.npy',allow_pickle=False))):
            raise ValueError('preselected ROI differs')
        ego=np.asarray(archived['frames'][frame]['T_world_from_vehicle'],float)
        for carrier in plan['carriers']:
            native=source/'native'/carrier
            report=json.loads((native/'report.json').read_text())
            if report['frame_index']!=frame or not report['normal_output_unchanged']:
                raise ValueError('native frame/parity differs')
            products={p['file']:load_product(native,p) for p in report['products']}
            config=report['config']
            origin,directions=virtual_rays(config,pixels,ego)
            analytic=intersect_plane(origin,directions)
            direct=arrays['plane-points.npy']
            truth_error=np.linalg.norm(analytic-direct,axis=-1)
            if truth_error.max()>plan['planar_tolerance_m']:
                raise ValueError('Blender/analytical plane truth exceeds tolerance')
            native_points=transform_points(ego,products['carrier-positions.f32'][y,x])
            carrier_error=np.linalg.norm(native_points-direct,axis=-1)
            regions=products['carrier-regions.u8'][y,x,0]
            gaps=[];source_errors=[];inverse_errors=[];valid_count=0
            for i,camera in enumerate(config['cameras']):
                status=products[f'camera{i}-status.u8'][y,x,0]
                valid=status==1;valid_count+=int(valid.sum())
                uv=products[f'camera{i}-pixels.f32'][y,x]
                correct,correct_valid=project(camera,transform_points(np.linalg.inv(ego),direct))
                overlap=valid & correct_valid
                gaps.extend(np.linalg.norm(uv[overlap]-correct[overlap],axis=-1))
                hits=arrays[carrier+'-hits.npy'][:,i]
                if not np.isfinite(hits[valid]).all():
                    raise ValueError('valid source ray has no plane hit')
                source_errors.extend(np.linalg.norm(hits[valid]-direct[valid],axis=-1))
                optical=camera['projection'];p=uv[valid]
                ab=(p-[optical['cx'],optical['cy']])/[optical['fx'],optical['fy']]
                theta=np.linalg.norm(ab,axis=-1)
                scale=np.divide(np.sin(theta),theta,out=np.ones_like(theta),where=theta>1e-14)
                local=np.column_stack([ab*scale[:,None],np.cos(theta)])
                pose=ego@np.linalg.inv(camera['T_camera_from_vehicle'])
                expected=intersect_plane(pose[:3,3],local@pose[:3,:3].T)
                inverse_errors.extend(np.linalg.norm(expected-hits[valid],axis=-1))
            source_errors=np.asarray(source_errors)
            if not valid_count or not len(gaps) or max(inverse_errors)>plan['planar_tolerance_m']:
                raise ValueError('empty control or source ray oracle mismatch')
            strict=carrier!='bowl'
            passed=(carrier_error.max()<=plan['planar_tolerance_m'] and source_errors.max()<=plan['planar_tolerance_m'] and max(gaps)<=plan['uv_tolerance_px'])
            if strict and not passed:
                raise ValueError('exact floor planar control failed: '+carrier)
            rows.append({'frame_index':frame,'carrier':carrier,'pixels':len(pixels),'source_pairs':len(pixels)*4,
                         'valid_pairs':valid_count,'planar_geometry_required':strict,'planar_tolerances_passed':bool(passed),
                         'source_matches_002m':int((source_errors<=.02).sum()),
                         'carrier_regions':{str(k):int((regions==k).sum()) for k in np.unique(regions)},
                         'carrier_to_plane_m':describe(carrier_error),'source_to_direct_plane_m':describe(source_errors),
                         'correct_vs_native_uv_px':describe(gaps),'blender_analytic_direct_m':describe(truth_error),
                         'blender_analytic_source_m':describe(inverse_errors),
                         'native_report_sha256':sha(native/'report.json'),'normal_rgba_sha256':sha(native/'output.rgba')})
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),
            'collector_sha256':sha(__file__),'helper_sha256':scene['helper_sha256'],'scene_truth_sha256':sha(scene_path),
            'blender_version':scene['blender_version'],'rows':rows,'limitations':plan['limitations']}
