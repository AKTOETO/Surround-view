"""Four source-texel center rays on a frozen scene; does not identify filtered color surfaces."""
import json
from pathlib import Path
import bpy
from mathutils import Matrix
import numpy as np

from diagnostic import build_diagnostic
from geometry_truth import opaque_hit
from paired_truth import virtual_pose
from source_surface_controls import matrix,sha
from source_footprint_audit import footprint,selected_pixels


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
    prior=json.loads((root/plan['displaced_baseline']).read_text())
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
            for carrier in plan['carriers']:
                native=root/plan['native_root']/f'frame{frame_index}'/'native'/carrier
                report_path=native/'report.json';metadata=json.loads(report_path.read_text())
                for a,b in zip(metadata['config']['cameras'],config['cameras']):
                    a,b=dict(a),dict(b)
                    if not np.allclose(a.pop('T_camera_from_vehicle'),b.pop('T_camera_from_vehicle'),atol=1e-12,rtol=0) or a!=b:
                        raise ValueError('native/truth camera differs')
                if metadata['frame_index']!=frame_index:raise ValueError('native frame differs')
                products={p['file']:p for p in metadata['products']}
                old=next(r for r in prior['rows'] if r['frame_index']==frame_index and r['carrier']==carrier)
                if sha(report_path)!=old['native_report_sha256']:raise ValueError('native report differs')
                previous=root/plan['displaced_root']/f'frame{frame_index}'/carrier
                arrays={name:np.load(previous/name,allow_pickle=False) for name in old['scene_record']['files']}
                for name,digest in old['scene_record']['files'].items():
                    if sha(previous/name)!=digest:raise ValueError('old ray arrays differ')
                coordinates=arrays['pixels.npy'];y,x=coordinates.T
                status=np.stack([matrix(native,products[f'camera{i}-status.u8'])[y,x,0] for i in range(4)],axis=1)
                indices,mismatch=selected_pixels(arrays['source-kinds.npy'],status,plan['control_pixels_per_case'])
                coordinates=coordinates[indices];status=status[indices];y,x=coordinates.T
                hits=np.full((len(coordinates),4,3),np.nan);kinds=np.zeros((len(coordinates),4),np.uint8)
                texels=np.full((len(coordinates),4,4,2),-1,np.int64)
                weights=np.zeros((len(coordinates),4,4),np.float64)
                texel_kinds=np.zeros((len(coordinates),4,4),np.uint8)
                for i,camera in enumerate(config['cameras']):
                    optics=camera['projection']
                    if any(optics['k']) or optics.get('alpha',0):raise ValueError('zero-distortion equidistant required')
                    pixels=matrix(native,products[f'camera{i}-pixels.f32'])[y,x]
                    valid=status[:,i]==1
                    camera_pose=pose@np.linalg.inv(camera['T_camera_from_vehicle'])
                    def sample(uv):
                        u,v=uv;a,b=(u-optics['cx'])/optics['fx'],(v-optics['cy'])/optics['fy']
                        theta=np.hypot(a,b);scale=np.sin(theta)/theta if theta>1e-14 else 1.
                        direction=camera_pose[:3,:3]@np.array([a*scale,b*scale,np.cos(theta)])
                        return cast(camera_pose[:3,3],direction)
                    xy,wgt=footprint(pixels[valid],camera['resolution']['width'],camera['resolution']['height'])
                    texels[valid,i]=xy;weights[valid,i]=wgt
                    cache={}
                    for n in np.flatnonzero(valid):
                        kinds[n,i],hits[n,i]=sample(pixels[n])
                        for j,uv in enumerate(texels[n,i]):
                            key=tuple(uv)
                            if key not in cache:cache[key]=sample(uv)[0]
                            texel_kinds[n,i,j]=cache[key]
                if not np.array_equal(kinds,arrays['source-kinds.npy'][indices]):raise ValueError('old center identity changed')
                if not np.allclose(hits,arrays['source-hits.npy'][indices],atol=1e-5,rtol=0,equal_nan=True):raise ValueError('old center hit changed')
                directory=output/f'frame{frame_index}'/carrier;directory.mkdir(parents=True)
                files={}
                for name,array in [('pixels.npy',coordinates),('mismatch.npy',mismatch),('center-hits.npy',hits),('center-kinds.npy',kinds),('texels.npy',texels),('weights.npy',weights),('texel-kinds.npy',texel_kinds)]:
                    np.save(directory/name,array,allow_pickle=False);files[name]=sha(directory/name)
                records.append({'frame_index':frame_index,'carrier':carrier,'native_report_sha256':sha(report_path),'files':files})
        record={'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'helper_sha256':sha(__file__),'blender_version':bpy.app.version_string,'kinds':{'0':'no_hit','1':'target','2':'road','3':'ego','4':'other'},'records':records}
        (output/'scene-truth.json').write_text(json.dumps(record,indent=2)+'\n')
        return [{'frame':r['frame_index'],'carrier':r['carrier']} for r in records]
    finally:bpy.context.window.scene=original
