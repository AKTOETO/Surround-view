"""Paired-backend planar precision audit; uniform rounding is a partial model, not a raster emulator."""
import json
from pathlib import Path
import numpy as np

from source_correspondence import sha,verified,load_product,project,describe
from source_correspondence_controls import transform_points
from planar_correspondence import intersect_plane,virtual_rays


def view_projection(config):
    view=config['virtual_camera'];a,e=view['azimuth_rad'],view['elevation_rad']
    target=np.asarray(view.get('target_m',[0,0,0]),float)
    eye=target+view['distance_m']*np.array([np.cos(e)*np.cos(a),np.cos(e)*np.sin(a),np.sin(e)])
    forward=target-eye;forward/=np.linalg.norm(forward)
    up=np.array([0,1,0] if abs(forward[2])>.999999 else [0,0,1],float)
    right=np.cross(forward,up);right/=np.linalg.norm(right)
    rotation=np.row_stack([right,np.cross(right,forward),-forward])
    camera=np.eye(4);camera[:3,:3]=rotation;camera[:3,3]=-rotation@eye
    near,far=view['clip_m'];scale=1/np.tan(view['fov_y_rad']/2)
    projection=np.zeros((4,4));projection[0,0]=scale*config['output']['height']/config['output']['width']
    projection[1,1]=scale;projection[2,2]=-(far+near)/(far-near)
    projection[2,3]=-2*far*near/(far-near);projection[3,2]=-1
    return projection@camera


def plane_from_mvp(mvp,pixels,width,height):
    y,x=np.asarray(pixels).T
    xy=np.column_stack([2*(x+.5)/width-1,1-2*(y+.5)/height])
    inverse=np.linalg.inv(mvp)
    def unproject(z):
        clip=np.column_stack([xy,np.full(len(x),z),np.ones(len(x))])@inverse.T
        return clip[:,:3]/clip[:,3,None]
    near,far=unproject(-1),unproject(1)
    return intersect_plane(near,far-near)


def reproject(mvp,points,width,height):
    clip=np.column_stack([points,np.ones(len(points))])@mvp.T
    return np.column_stack([(clip[:,0]/clip[:,3]+1)*width/2-.5,
                           (1-clip[:,1]/clip[:,3])*height/2-.5])


def collect(root,plan_path):
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    output=root/plan['output_root']
    invocation_path=output/'invocations.json';invocations=json.loads(invocation_path.read_text())
    verified(output/'sv-correspondence-probe',plan['binary_sha256'])
    if invocations['binary_sha256']!=plan['binary_sha256'] or len(invocations['cases'])!=10 or any(c['returncode']!=0 for c in invocations['cases']):
        raise ValueError('backend capture provenance differs')
    truth=json.loads((root/plan['truth_path']).read_text())
    rows=[]
    for frame in plan['frames']:
        old=root/plan['reference_root']/f'frame{frame}'
        roi=np.argwhere(np.load(old/'direct-mask.npy',allow_pickle=False));y,x=roi.T
        ego=np.asarray(truth['frames'][frame]['T_world_from_vehicle'])
        for carrier in plan['carriers']:
            reference=json.loads((old/'native'/carrier/'report.json').read_text())
            directory=output/f'frame{frame}'/'native'/carrier
            mesa=json.loads((directory/'report.json').read_text())
            if (mesa['config_sha256']!=reference['config_sha256'] or mesa['manifest_sha256']!=reference['manifest_sha256']
                    or mesa['inputs']!=reference['inputs'] or mesa['frame_index']!=frame
                    or not mesa['normal_output_unchanged'] or 'llvmpipe' not in mesa['device'].lower()):
                raise ValueError('paired input/frame/device differs')
            for source in mesa['inputs']:verified(Path(source['path']),source['sha256'])
            config=reference['config'];w,h=config['output']['width'],config['output']['height']
            mvp=view_projection(config);rounded=mvp.astype(np.float32).astype(float)
            double_plane=plane_from_mvp(mvp,roi,w,h);float_plane=plane_from_mvp(rounded,roi,w,h)
            origin,direction=virtual_rays(config,roi,np.eye(4))
            if np.max(np.linalg.norm(double_plane-intersect_plane(origin,direction),axis=-1))>1e-10:
                raise ValueError('double MVP/independent virtual ray disagree')
            backend=[]
            captures=[]
            for name,metadata,native in [('nvidia',reference,old/'native'/carrier),('mesa',mesa,directory)]:
                products={p['file']:load_product(native,p) for p in metadata['products']}
                p=products['carrier-positions.f32'][y,x]
                captures.append(products)
                if not np.isfinite(p).all():raise ValueError('invalid carrier position in fixed ROI')
                hits=[];gaps=[];optical=[];rounding_uv=[]
                for i,camera in enumerate(config['cameras']):
                    status=products[f'camera{i}-status.u8'][y,x,0];valid=status==1
                    actual=products[f'camera{i}-pixels.f32'][y,x]
                    correct,correct_valid=project(camera,double_plane)
                    pc,pc_valid=project(camera,p)
                    if not np.array_equal(valid,pc_valid):raise ValueError('native source validity disagrees')
                    both=valid & correct_valid
                    gaps.extend(np.linalg.norm(actual[both]-correct[both],axis=-1))
                    optical.extend(np.linalg.norm(actual[valid]-pc[valid],axis=-1))
                    pf,_=project(camera,float_plane)
                    rounding_uv.extend(np.linalg.norm(pf[both]-correct[both],axis=-1))
                    optics=camera['projection'];uv=actual[valid]
                    ab=(uv-[optics['cx'],optics['cy']])/[optics['fx'],optics['fy']]
                    theta=np.linalg.norm(ab,axis=-1)
                    scale=np.divide(np.sin(theta),theta,out=np.ones_like(theta),where=theta>1e-14)
                    local=np.column_stack([ab*scale[:,None],np.cos(theta)])
                    pose=np.linalg.inv(camera['T_camera_from_vehicle'])
                    sample=intersect_plane(pose[:3,3],local@pose[:3,:3].T)
                    hits.extend(np.linalg.norm(sample-double_plane[valid],axis=-1))
                passed=max(hits)<=plan['planar_tolerance_m'] and max(gaps)<=plan['uv_tolerance_px']
                backend.append({'backend':name,'device':metadata['device'],'report_sha256':sha(native/'report.json'),
                                'normal_rgba_sha256':sha(native/'output.rgba'),'valid_pairs':len(hits),
                                'strict_pass':bool(passed),'carrier_to_ideal_plane_m':describe(np.linalg.norm(p-double_plane,axis=-1)),
                                'source_to_ideal_plane_m':describe(hits),'correct_vs_native_uv_px':describe(gaps),
                                'optical_residual_px':describe(optical),
                                'native_output_reprojection_px':describe(np.linalg.norm(reproject(mvp,p,w,h)-roi[:,::-1],axis=-1)),
                                'rounded_mvp_output_reprojection_px':describe(np.linalg.norm(reproject(rounded,p,w,h)-roi[:,::-1],axis=-1)),
                                'uniform_rounding_only_uv_px':describe(rounding_uv)})
            validity_equal=all(np.array_equal(captures[0][f'camera{i}-status.u8'][y,x],captures[1][f'camera{i}-status.u8'][y,x]) for i in range(4))
            rows.append({'frame_index':frame,'carrier':carrier,'pixels':len(roi),'planar_geometry_required':carrier!='bowl',
                         'source_validity_equal':validity_equal,'rgba_hash_equal':backend[0]['normal_rgba_sha256']==backend[1]['normal_rgba_sha256'],
                         'paired_carrier_distance_m':describe(np.linalg.norm(captures[0]['carrier-positions.f32'][y,x]-captures[1]['carrier-positions.f32'][y,x],axis=-1)),
                         'mvp_rounding_only_plane_m':describe(np.linalg.norm(float_plane-double_plane,axis=-1)),
                         'T_world_from_vehicle':ego.tolist(),'backends':backend})
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'collector_sha256':sha(__file__),
            'binary_sha256':plan['binary_sha256'],'invocations_sha256':sha(invocation_path),
            'rows':rows,'limitations':plan['limitations']}
