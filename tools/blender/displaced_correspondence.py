"""Nearest virtual/source scene surfaces for actual displaced coded-color output pixels."""
import json
from pathlib import Path
import bpy
from mathutils import Matrix
import numpy as np

from diagnostic import build_diagnostic
from geometry_truth import opaque_hit
from paired_truth import virtual_pose
from source_surface_controls import matrix,sha


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
                rgb=matrix(native,products['output.rgba'])[...,:3].astype(float)/255
                mask=np.minimum(rgb[...,0],rgb[...,2])-rgb[...,1]>plan['chroma_threshold']
                coordinates=np.argwhere(mask);h,w=mask.shape
                direct=np.full((len(coordinates),3),np.nan);direct_kind=np.zeros(len(coordinates),np.uint8)
                near,far=config['virtual_camera']['clip_m'];tangent=np.tan(config['virtual_camera']['fov_y_rad']/2)
                for n,(y,x) in enumerate(coordinates):
                    local=np.array([(2*(x+.5)/w-1)*tangent*w/h,(1-2*(y+.5)/h)*tangent,-1.])
                    direction=virtual[:3,:3]@local;length=np.linalg.norm(direction);direction/=length
                    direct_kind[n],direct[n]=cast(virtual[:3,3]+direction*near*length,direction,(far-near)*length)
                hits=np.full((len(coordinates),4,3),np.nan);kinds=np.zeros((len(coordinates),4),np.uint8)
                for i,camera in enumerate(config['cameras']):
                    optics=camera['projection']
                    if any(optics['k']) or optics.get('alpha',0):raise ValueError('zero-distortion equidistant required')
                    pixels=matrix(native,products[f'camera{i}-pixels.f32'])
                    status=matrix(native,products[f'camera{i}-status.u8'])[...,0]
                    camera_pose=pose@np.linalg.inv(camera['T_camera_from_vehicle'])
                    for n,(y,x) in enumerate(coordinates):
                        if status[y,x]!=1:continue
                        u,v=pixels[y,x];a,b=(u-optics['cx'])/optics['fx'],(v-optics['cy'])/optics['fy']
                        theta=np.hypot(a,b);scale=np.sin(theta)/theta if theta>1e-14 else 1.
                        direction=camera_pose[:3,:3]@np.array([a*scale,b*scale,np.cos(theta)])
                        kinds[n,i],hits[n,i]=cast(camera_pose[:3,3],direction)
                directory=output/f'frame{frame_index}'/carrier;directory.mkdir(parents=True)
                files={}
                for name,array in [('pixels.npy',coordinates),('virtual-points.npy',direct),('virtual-kinds.npy',direct_kind),('source-hits.npy',hits),('source-kinds.npy',kinds)]:
                    np.save(directory/name,array,allow_pickle=False);files[name]=sha(directory/name)
                records.append({'frame_index':frame_index,'carrier':carrier,'native_report_sha256':sha(report_path),'files':files})
        record={'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'helper_sha256':sha(__file__),'blender_version':bpy.app.version_string,'kinds':{'0':'no_hit','1':'target','2':'road','3':'ego','4':'other'},'records':records}
        (output/'scene-truth.json').write_text(json.dumps(record,indent=2)+'\n')
        return [{'frame':r['frame_index'],'carrier':r['carrier']} for r in records]
    finally:bpy.context.window.scene=original
