"""Isolated Blender scene truth for exhaustive remaining reference-ray coordinates."""
import json
from pathlib import Path
import bpy
from mathutils import Matrix
import numpy as np
from diagnostic import build_diagnostic
from geometry_truth import opaque_hit
from paired_truth import virtual_pose
from visibility import visible_camera_bits
from source_surface_truth import sha


def capture(root,plan_path):
    root,plan_path=Path(root),Path(plan_path);plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():
        if sha(root/name)!=digest:raise ValueError('frozen input changed: '+name)
    out=root/plan['output_root'];out.mkdir(exist_ok=False)
    inputs=json.loads((root/plan['prepared_input']).read_text())
    truth_path=root/plan['truth_path'];archived=json.loads(truth_path.read_text())
    case=json.loads((root/plan['case_plan']).read_text())['cases'][0]
    provenance=json.loads((root/plan['generator_provenance']).read_text())
    for name,digest in provenance['generator_sha256'].items():
        if sha(Path(__file__).with_name(name))!=digest:raise ValueError('scene generator changed: '+name)
    original=bpy.context.window.scene;frames=[]
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
        for data in inputs['frames']:
            f=data['frame'];frame=archived['frames'][f]
            scene.frame_set(1+f*case['capture']['frame_step']);ego.matrix_world=Matrix(frame['T_world_from_vehicle'])
            bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
            if not np.allclose(target.location,frame['diagnostic_target_position_m'],atol=1e-5,rtol=0):raise ValueError('target pose differs')
            cfg=archived['config'];generated=json.loads(scene['sv_config'])
            roundtrip=0.
            for actual,old,new in zip(data['config']['cameras'],cfg['cameras'],generated['cameras'],strict=True):
                actual,old=dict(actual),dict(old)
                ta,to=np.asarray(actual.pop('T_camera_from_vehicle')),np.asarray(old.pop('T_camera_from_vehicle'))
                roundtrip=max(roundtrip,float(np.max(np.abs(ta-to))))
                if actual!=old or roundtrip>1e-12 or not np.allclose(to,new['T_camera_from_vehicle'],atol=1e-10,rtol=0):raise ValueError('camera calibration differs')
            pose=np.asarray(frame['T_world_from_vehicle']);virtual=pose@virtual_pose(cfg)
            h,w=data['config']['output']['height'],data['config']['output']['width']
            if cfg['output']!=data['config']['output']:raise ValueError('virtual raster differs')
            for key in ['objects','visibility']:
                p=truth_path.parent/frame[key]
                if sha(p)!=archived['sha256'][frame[key]]:raise ValueError('archived truth payload differs')
            old_ids=np.load(truth_path.parent/frame['objects'],allow_pickle=False)
            old_visibility=np.load(truth_path.parent/frame['visibility'],allow_pickle=False)
            target_id=next(v for k,v in archived['objects'].items() if 'coded object target' in k)
            legacy=old_ids==target_id;mask=np.zeros((h,w),bool);hits={};rays=[]
            def kind(obj):return 1 if obj==target else 2 if obj==road else 3 if obj in keep-{target,road} else 4
            def cast(o,d,distance=200.):
                d=np.asarray(d,float);d=d/np.linalg.norm(d)
                result=opaque_hit(scene,deps,o,d,distance)
                hit=dict(kind=kind(result[4].original),point=list(result[1]),polygon=int(result[3])) if result[0] else dict(kind=0,point=None,polygon=None)
                return d,hit
            tangent=np.tan(cfg['virtual_camera']['fov_y_rad']/2);near,far=cfg['virtual_camera']['clip_m']
            selected={tuple(p):i for i,p in enumerate(data['coordinates_yx'])}
            # Full independent mask, not validation restricted to selected residual points.
            for y in range(h):
                for x in range(w):
                    d=virtual[:3,:3]@np.array([(2*(x+.5)/w-1)*tangent*w/h,(1-2*(y+.5)/h)*tangent,-1.])
                    length=np.linalg.norm(d);d/=length;o=virtual[:3,3]+d*near*length;distance=(far-near)*length
                    d,hit=cast(o,d,distance)
                    if hit['kind']==1:mask[y,x]=True;hits[(y,x)]=hit['point']
                    if (y,x) in selected:rays.append(dict(name=f'virtual-{selected[(y,x)]}',origin=o.tolist(),direction=d.tolist(),max_distance_m=float(distance),hit=hit))
            if not np.array_equal(mask,legacy):raise ValueError('full reconstructed target mask differs')
            new_visibility=np.zeros((h,w),np.uint8)
            def nearest(o,d,distance):return cast(o,d,distance)[1]['point']
            for y,x in np.argwhere(mask):new_visibility[y,x]=visible_camera_bits(hits[(y,x)],pose,cfg['cameras'],nearest)
            if not np.array_equal(new_visibility[mask],old_visibility[mask]):raise ValueError('full target visibility differs')
            q=np.asarray(data['carrier_xyz_vehicle_m']);world=q@pose[:3,:3].T+pose[:3,3]
            max_uv_error=0.
            for index in range(len(selected)):
                for camera,c in enumerate(cfg['cameras']):
                    if not data['independent_valid'][index][camera]:continue
                    native=data['native_uv'][index][camera];uv=np.asarray(native if native is not None else data['independent_uv'][index][camera],float)
                    if native is not None:max_uv_error=max(max_uv_error,float(np.max(np.abs(uv-data['independent_uv'][index][camera]))))
                    k=c['projection'];cp=pose@np.linalg.inv(np.asarray(c['T_camera_from_vehicle']))
                    def source_ray(px):
                        a,b=(np.asarray(px)-[k['cx'],k['cy']])/[k['fx'],k['fy']];theta=np.hypot(a,b)
                        scale=np.sin(theta)/theta if theta>1e-14 else 1
                        return cp[:3,:3]@np.array([a*scale,b*scale,np.cos(theta)])
                    size=c['resolution'];base=np.floor(uv);fraction=uv-base
                    texels=[];coefficients=[]
                    for dx,dy in [(0,0),(1,0),(0,1),(1,1)]:
                        texels.append(np.clip(base+[dx,dy],[0,0],[size['width']-1,size['height']-1]))
                        coefficients.append(float((fraction[0] if dx else 1-fraction[0])*(fraction[1] if dy else 1-fraction[1])))
                    for name,px in [(f'source-{index}-{camera}',uv)]+[(f'texel-{index}-{camera}-{j}',p) for j,p in enumerate(texels)]:
                        d,hit=cast(cp[:3,3],source_ray(px))
                        record=dict(name=name,origin=cp[:3,3].tolist(),direction=d.tolist(),max_distance_m=200.,hit=hit)
                        if name.startswith('source'):record.update(uv=uv.tolist(),uv_kind='native_retained' if native is not None else 'independent_projection',bilinear_coefficients=coefficients)
                        rays.append(record)
            if max_uv_error>plan['native_projection_tolerance_px']:raise ValueError('native independent UV differs')
            triangles=[];kinds=[];polygons=[]
            for obj in scene.objects:
                if obj.type!='MESH' or obj.hide_render:continue
                evaluated=obj.evaluated_get(deps);mesh=evaluated.to_mesh()
                try:
                    mesh.calc_loop_triangles()
                    for triangle in mesh.loop_triangles:
                        triangles.append([list(evaluated.matrix_world@mesh.vertices[v].co) for v in triangle.vertices])
                        kinds.append(kind(obj));polygons.append(int(triangle.polygon_index))
                finally:evaluated.to_mesh_clear()
            files={}
            for name,array in [(f'frame{f}-triangles.npy',np.asarray(triangles,np.float64)),(f'frame{f}-kinds.npy',np.asarray(kinds,np.uint8)),(f'frame{f}-polygons.npy',np.asarray(polygons,np.int32))]:
                np.save(out/name,array,allow_pickle=False);files[name]=sha(out/name)
            frames.append(dict(frame=f,triangles=f'frame{f}-triangles.npy',triangle_kinds=f'frame{f}-kinds.npy',files=files,
                triangle_count=len(triangles),direct_target_mask_equal=True,target_visibility_equal=True,target_pixels=int(mask.sum()),
                camera_roundtrip_error=roundtrip,native_projection_max_error_px=max_uv_error,carrier_xyz_world_m=world.tolist(),rays=rays))
        record=dict(schema_version=1,scope=plan['scope'],plan_sha256=sha(plan_path),helper_sha256=sha(__file__),blender_version=bpy.app.version_string,
            frames=frames,kinds=dict(no_hit=0,target=1,road=2,ego=3,other=4))
        (out/'scene-truth.json').write_text(json.dumps(record,indent=2,allow_nan=False)+'\n')
    finally:bpy.context.window.scene=original
