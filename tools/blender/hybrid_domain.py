"""Independent virtual/source/four-texel hits on the union of new hybrid chroma coordinates."""
import json
from pathlib import Path
import bpy
from mathutils import Matrix
import numpy as np

from diagnostic import build_diagnostic
from geometry_truth import opaque_hit
from paired_truth import virtual_pose
from source_surface_controls import matrix,sha
from source_footprint_audit import footprint
from cube_footprint_audit import fisheye_ray


def capture(root,plan_path):
    root,plan_path=Path(root),Path(plan_path);plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():
        if sha(root/name)!=digest:raise ValueError('frozen input changed: '+name)
    output=root/plan['output_root'];output.mkdir(exist_ok=False)
    inputs=root/plan['input_root'];case=json.loads((inputs/'plan.json').read_text())['cases'][0]
    archived=json.loads((inputs/'seed101-capture/paired_truth.json').read_text())
    provenance=json.loads((inputs.parent/'provenance.json').read_text())
    for name,digest in provenance['generator_sha256'].items():
        if sha(Path(__file__).with_name(name))!=digest:raise ValueError('scene generator differs')
    requests=json.loads((root/plan['requests']).read_text())
    old_baseline=json.loads((root/plan['displaced_baseline']).read_text())
    original=bpy.context.window.scene;records=[]
    try:
        scene=build_diagnostic(case);bpy.context.window.scene=scene
        ego=scene.objects[scene['sv_ego']]
        target=scene.objects[json.loads(scene['sv_diagnostic_target'])['object_name']]
        road=next(o for o in scene.objects if o.name.split('.')[0]=='SV road')
        keep={ego,*ego.children_recursive,target,road}
        for obj in list(scene.objects):
            if obj.type=='MESH' and obj not in keep:bpy.data.objects.remove(obj,do_unlink=True)
        master=json.loads((root/'assets/scenarios/parallax-sensor-v1.json').read_text())
        road.scale.y=master['ground_size_m'][1]/12.;road.scale.x=master['ground_size_m'][0]/100.
        for frame_index in plan['frames']:
            frame=archived['frames'][frame_index]
            scene.frame_set(1+frame_index*case['capture']['frame_step'])
            ego.matrix_world=Matrix(frame['T_world_from_vehicle']);bpy.context.view_layer.update()
            if not np.allclose(target.location,frame['diagnostic_target_position_m'],atol=1e-5,rtol=0):
                raise ValueError('target pose differs')
            config=archived['config'];generated=json.loads(scene['sv_config'])
            for old,new in zip(config['cameras'],generated['cameras']):
                if not np.allclose(old['T_camera_from_vehicle'],new['T_camera_from_vehicle'],atol=1e-10,rtol=0):
                    raise ValueError('reconstructed rig differs')
            pose=np.asarray(frame['T_world_from_vehicle']);virtual=pose@virtual_pose(config)
            deps=bpy.context.evaluated_depsgraph_get()
            def cast(origin,direction,distance=200.):
                result=opaque_hit(scene,deps,origin,direction,distance)
                if not result[0]:return 0,np.full(3,np.nan)
                obj=result[4].original
                kind=1 if obj==target else 2 if obj==road else 3 if obj in keep-{target,road} else 4
                return kind,np.asarray(result[1])
            for group in requests['groups']:
                if group['frame']!=frame_index:continue
                carrier=group['carrier'];coordinates=np.asarray(group['pixels_yx'],np.int64)
                index_path=root/group['index_path']
                if sha(index_path)!=group['index_sha256']:raise ValueError('hybrid index differs')
                index=json.loads(index_path.read_text());h,w=index['height'],index['width']
                for a,b in zip(index['config']['cameras'],config['cameras']):
                    a,b=dict(a),dict(b)
                    if not np.allclose(a.pop('T_camera_from_vehicle'),b.pop('T_camera_from_vehicle'),atol=1e-12,rtol=0) or a!=b:
                        raise ValueError('native/truth camera differs')
                direct=np.full((len(coordinates),3),np.nan);direct_kind=np.zeros(len(coordinates),np.uint8)
                near,far=config['virtual_camera']['clip_m'];tangent=np.tan(config['virtual_camera']['fov_y_rad']/2)
                for n,(y,x) in enumerate(coordinates):
                    local=np.array([(2*(x+.5)/w-1)*tangent*w/h,(1-2*(y+.5)/h)*tangent,-1.])
                    direction=virtual[:3,:3]@local;length=np.linalg.norm(direction);direction/=length
                    direct_kind[n],direct[n]=cast(virtual[:3,3]+direction*near*length,direction,(far-near)*length)
                hits=np.full((len(coordinates),4,3),np.nan);kinds=np.zeros((len(coordinates),4),np.uint8)
                texels=np.full((len(coordinates),4,4,2),-1,np.int64)
                texel_weights=np.zeros((len(coordinates),4,4));texel_kinds=np.zeros((len(coordinates),4,4),np.uint8)
                status=np.asarray(group['status']);pixels=np.asarray(group['uv'])
                for i,camera in enumerate(config['cameras']):
                    camera_pose=pose@np.linalg.inv(camera['T_camera_from_vehicle'])
                    def sample(uv):
                        return cast(camera_pose[:3,3],camera_pose[:3,:3]@fisheye_ray(uv,camera['projection']))
                    valid=status[:,i]==1
                    xy,wgt=footprint(pixels[valid,i],camera['resolution']['width'],camera['resolution']['height'])
                    texels[valid,i]=xy;texel_weights[valid,i]=wgt;cache={}
                    for n in np.flatnonzero(valid):
                        kinds[n,i],hits[n,i]=sample(pixels[n,i])
                        for j,uv in enumerate(texels[n,i]):
                            key=tuple(uv)
                            if key not in cache:cache[key]=sample(uv)[0]
                            texel_kinds[n,i,j]=cache[key]
                # Recheck the first32 archived displaced pixels, independent of new-domain selection.
                old_dir=root/plan['displaced_root']/f'frame{frame_index}'/carrier
                old_record=next(r for r in old_baseline['rows'] if r['frame_index']==frame_index and r['carrier']==carrier)
                for name,digest in old_record['scene_record']['files'].items():
                    if sha(old_dir/name)!=digest:raise ValueError('archived controls differ')
                anchor_xy=np.load(old_dir/'pixels.npy',allow_pickle=False)[:32]
                old_kinds=np.load(old_dir/'source-kinds.npy',allow_pickle=False)[:32]
                old_hits=np.load(old_dir/'source-hits.npy',allow_pickle=False)[:32]
                native=root/plan['reference_root']/f'frame{frame_index}'/'native'/carrier
                meta=json.loads((native/'report.json').read_text());products={p['file']:p for p in meta['products']}
                checks=0;max_error=0.
                for i,camera in enumerate(config['cameras']):
                    camera_pose=pose@np.linalg.inv(camera['T_camera_from_vehicle'])
                    uv=matrix(native,products[f'camera{i}-pixels.f32'])
                    st=matrix(native,products[f'camera{i}-status.u8'])
                    for n,(y,x) in enumerate(anchor_xy):
                        if st[y,x,0]!=1:continue
                        kind,hit=cast(camera_pose[:3,3],camera_pose[:3,:3]@fisheye_ray(uv[y,x],camera['projection']))
                        if kind!=old_kinds[n,i]:raise ValueError('anchor kind differs')
                        error=float(np.linalg.norm(hit-old_hits[n,i]))
                        if not np.isfinite(error) or error>plan['anchor_hit_tolerance_m']:raise ValueError('anchor hit differs')
                        max_error=max(max_error,error);checks+=1
                directory=output/f'frame{frame_index}'/carrier;directory.mkdir(parents=True)
                files={}
                for name,array in [('pixels.npy',coordinates),('virtual-hits.npy',direct),('virtual-kinds.npy',direct_kind),
                                   ('source-hits.npy',hits),('source-kinds.npy',kinds),('texels.npy',texels),
                                   ('texel-weights.npy',texel_weights),('texel-kinds.npy',texel_kinds)]:
                    np.save(directory/name,array,allow_pickle=False);files[name]=sha(directory/name)
                records.append({'frame':frame_index,'carrier':carrier,'files':files,'anchor_pairs':checks,'anchor_max_hit_error_m':max_error})
        result={'schema_version':1,'plan_sha256':sha(plan_path),'helper_sha256':sha(__file__),'blender_version':bpy.app.version_string,'groups':records}
        (output/'scene-truth.json').write_text(json.dumps(result,indent=2)+'\n')
        return [{'frame':g['frame'],'carrier':g['carrier'],'anchor_pairs':g['anchor_pairs']} for g in records]
    finally:bpy.context.window.scene=original
