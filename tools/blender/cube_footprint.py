"""Independent central and 8x8 stratified cube pixel rays; no Eevee filter emulation."""
import json
from pathlib import Path
import bpy
from mathutils import Matrix
import numpy as np

from diagnostic import build_diagnostic
from geometry_truth import opaque_hit
from paired_truth import virtual_pose
from source_surface_controls import matrix,sha
from cube_footprint_audit import fisheye_ray,cube_ray


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
    original=bpy.context.window.scene;records={}
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
            cache={}
            for key,q in requests['requests'].items():
                if q['frame']!=frame_index:continue
                camera=config['cameras'][q['camera']]
                camera_pose=pose@np.linalg.inv(camera['T_camera_from_vehicle'])
                origin=camera_pose[:3,3]
                def sample_cube(pixel):
                    local=cube_ray(q['face'],pixel,requests['face_size'])
                    direction=camera_pose[:3,:3]@local
                    return cast(origin,direction)[0]
                center_kind=cast(origin,camera_pose[:3,:3]@fisheye_ray(q['pixel'],camera['projection']))[0]
                centers=[];grid_hits=[]
                for pixel in q['texels']:
                    identity=(q['camera'],q['face'],*pixel)
                    if identity not in cache:
                        center=sample_cube(pixel);count=0
                        for iy in range(plan['area_grid_size']):
                            for ix in range(plan['area_grid_size']):
                                offset=(np.array([ix,iy])+.5)/plan['area_grid_size']-.5
                                count+=sample_cube(np.asarray(pixel)+offset)==1
                        cache[identity]=(center,count)
                    center,count=cache[identity];centers.append(center);grid_hits.append(count)
                records[key]={'source_center_kind':center_kind,'cube_center_kinds':centers,'cube_grid_target_hits':grid_hits}
        result={'schema_version':1,'plan_sha256':sha(plan_path),'helper_sha256':sha(__file__),
                'blender_version':bpy.app.version_string,'cameras':config['cameras'],'area_grid_size':plan['area_grid_size'],'requests':records}
        (output/'scene-truth.json').write_text(json.dumps(result,indent=2)+'\n')
        return {'unique_source_texels':len(records),'blender_version':bpy.app.version_string}
    finally:bpy.context.window.scene=original
