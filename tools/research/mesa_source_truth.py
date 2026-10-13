"""Matched Mesa UV/source-surface truth using frozen exported Blender geometry."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image
from source_correspondence import sha, verified, load_product
from residual_surface import inverse_ray, trace_triangles
from planar_correspondence import virtual_rays
from source_disagreement import bilinear_rgb


def virtual_segment(config, pixel, pose):
    eye, directions = virtual_rays(config, np.asarray([pixel],float),pose)
    ray=directions[0];length=np.linalg.norm(ray)
    near,far=config['virtual_camera']['clip_m']
    return eye+near*ray,ray/length,(far-near)*length


def matching(source, virtual, tolerances):
    error=(float(np.linalg.norm(np.asarray(source['point'])-virtual['point']))
           if source['point'] is not None and virtual['point'] is not None else None)
    return error,{str(t):bool(source['kind']!=0 and source['kind']==virtual['kind'] and error<=t) for t in tolerances}


def checked(root,path):
    plan=json.loads(path.read_text())
    for name,digest in plan['frozen_files'].items(): verified(root/name,digest)
    return plan


def capture(root,path):
    plan=checked(root,path);out=root/plan['output_root'];out.mkdir(exist_ok=False)
    calls=[]
    for case in plan['cases']:
        directory=out/f"frame{case['frame']}-{case['variant']}"
        result=subprocess.run([str(root/plan['binary']),str(root/case['config']),str(root/plan['manifest']),str(directory),str(case['frame'])],
            env=dict(os.environ,**plan['backend_environment']),capture_output=True,text=True,timeout=60)
        calls.append(dict(frame=case['frame'],variant=case['variant'],directory=str(directory.relative_to(root)),returncode=result.returncode,stderr=result.stderr))
        if result.returncode:
            (out/'invocations.json').write_text(json.dumps(calls,indent=2)+'\n')
            raise RuntimeError(result.stderr)
    (out/'invocations.json').write_text(json.dumps(calls,indent=2)+'\n')


def collect(root,path):
    plan=checked(root,path)
    calls_path=root/plan['output_root']/'invocations.json'
    calls=json.loads(calls_path.read_text())
    if [(c['frame'],c['variant']) for c in calls]!=[(c['frame'],c['variant']) for c in plan['cases']]:
        raise ValueError('capture selection differs')
    scene_path=root/plan['scene_truth'];scene=json.loads(scene_path.read_text())
    archive=json.loads((root/plan['paired_truth']).read_text())
    old=json.loads((root/plan['historical_truth']).read_text())
    metric_calls=json.loads((root/plan['metric_invocations']).read_text())
    old_input=json.loads((root/plan['historical_prepared_input']).read_text())
    frames={};anchors=[]
    for f in scene['frames']:
        if not f['direct_target_mask_equal'] or not f['target_visibility_equal']:
            raise ValueError('unverified scene reconstruction')
        triangles=np.load(scene_path.parent/f['triangles'],allow_pickle=False)
        kinds=np.load(scene_path.parent/f['triangle_kinds'],allow_pickle=False)
        pose=np.asarray(archive['frames'][f['frame']]['T_world_from_vehicle'])
        for ray in f['rays']:
            independent=trace_triangles(ray['origin'],ray['direction'],triangles,kinds,ray['max_distance_m'])
            expected=ray['hit'];error,_=matching(independent,expected,[plan['scene_anchor_tolerance_m']])
            passed=independent['kind']==expected['kind'] and (error is None or error<=plan['scene_anchor_tolerance_m'])
            if (independent['point'] is None)!=(expected['point'] is None): passed=False
            anchors.append(dict(frame=f['frame'],name=ray['name'],point_error_m=error,passed=passed))
        frames[f['frame']]=(triangles,kinds,pose)
    primary=[];inventory=[];captures=[];points_by_frame={};uv_by_frame={};virtual_cache={};source_cache={}
    for case,call in zip(plan['cases'],calls,strict=True):
        if call['returncode']: raise ValueError('failed native capture')
        directory=root/call['directory'];meta_path=directory/'report.json';m=json.loads(meta_path.read_text())
        old_meta=json.loads((root/case['historical_report']).read_text())
        mc=next(c for c in metric_calls if (c['frame'],c['variant'])==(case['frame'],case['variant']))
        mm=mc['metadata'];cfg=m['config'];h,w=cfg['output']['height'],cfg['output']['width']
        camera_delta=0.
        for actual,expected in zip(cfg['cameras'],archive['config']['cameras'],strict=True):
            actual,expected=dict(actual),dict(expected)
            ta=np.asarray(actual.pop('T_camera_from_vehicle'));te=np.asarray(expected.pop('T_camera_from_vehicle'))
            camera_delta=max(camera_delta,float(np.max(np.abs(ta-te))))
            if actual!=expected or camera_delta>plan['camera_identity_tolerance']:
                raise ValueError('physical camera identity differs')
        if (plan['required_device_substring'] not in m['device'] or not m['normal_output_unchanged']
                or m['source_fingerprint']!=plan['source_fingerprint'] or m['source_fingerprint']!=mm['source_fingerprint']
                or m['config_sha256']!=sha(root/case['config']) or m['config_sha256']!=mm['config_sha256']
                or m['manifest_sha256']!=sha(root/plan['manifest']) or m['manifest_sha256']!=mm['manifest_sha256']
                or m['frame_index']!=case['frame'] or m['frame_index']!=mm['frame_index']
                or m['inputs']!=old_meta['inputs'] or m['inputs']!=mm['inputs']
                or cfg['virtual_camera']!=archive['config']['virtual_camera']
                or cfg['output']!=archive['config']['output']):
            raise ValueError('native/metric/scene identity differs')
        p={v['file']:load_product(directory,v) for v in m['products']}
        metric=np.fromfile(verified(root/mc['file'],mm['payload_sha256']),np.uint8).reshape(h,w,4)
        images={i['camera_id']:np.asarray(Image.open(verified(Path(i['path']),i['sha256'])).convert('RGB')) for i in m['inputs']}
        frame=case['frame'];triangles,kinds,pose=frames[frame]
        coords=old_input['frames'][frame]['coordinates_yx']
        grid=([(y,x) for y in range(plan['grid_offset'],h,plan['grid_step']) for x in range(plan['grid_offset'],w,plan['grid_step'])]
              if case['variant'] in plan['inventory_profiles'] else [])
        selected=set(map(tuple,coords))|set(grid)
        for y,x in sorted(selected):
            region=int(p['carrier-regions.u8'][y,x,0]);domain=region not in [0,3,5]
            q=p['carrier-positions.f32'][y,x]
            eligible=[i for i in range(4) if p[f'camera{i}-status.u8'][y,x,0]==1]
            sources=[];colors=[]
            virtual=None
            if domain:
                key=(frame,y,x)
                if key not in virtual_cache:
                    origin,direction,distance=virtual_segment(cfg,[y,x],pose)
                    virtual_cache[key]=trace_triangles(origin,direction,triangles,kinds,distance)
                virtual=virtual_cache[key]
                for camera in eligible:
                    uv=p[f'camera{camera}-pixels.f32'][y,x]
                    cachekey=(frame,camera,*uv.view(np.uint32).tolist())
                    if cachekey not in source_cache:
                        origin,direction=inverse_ray(cfg['cameras'][camera],uv,pose)
                        source_cache[cachekey]=trace_triangles(origin,direction,triangles,kinds)
                    hit=source_cache[cachekey];error,matched=matching(hit,virtual,plan['match_tolerances_m'])
                    rgb=bilinear_rgb(images[camera],uv);colors.append(rgb)
                    sources.append(dict(camera=camera,uv=uv.tolist(),kind=hit['kind'],point=hit['point'],distance_to_virtual_hit_m=error,matched=matched,rgb_byte=rgb.tolist()))
            elif eligible: raise ValueError('eligible sample outside current carrier domain')
            expected_range=float(np.ptp(colors,axis=0).max()) if len(colors)>=2 else None
            byte=int(np.floor(expected_range+.5)) if expected_range is not None else 0
            measured=metric[y,x].tolist()
            passed=(measured[1]==len(eligible) and measured[2]==(255 if len(eligible)>=2 else 0)
                    and measured[3]==(255 if domain else 0) and abs(measured[0]-byte)<=plan['rgb_byte_tolerance'])
            row=dict(frame=frame,variant=case['variant'],y=y,x=x,carrier_region=region,carrier_xyz=q.tolist() if domain else None,
                virtual_kind=virtual['kind'] if virtual else None,virtual_point=virtual['point'] if virtual else None,
                eligible_cameras=eligible,sources=sources,native_pixel=measured,independent_range_byte=expected_range,
                metric_oracle_passed=passed,byte_error=abs(measured[0]-byte),
                matched_eligible={str(t):[s['camera'] for s in sources if s['matched'][str(t)]] for t in plan['match_tolerances_m']})
            if (y,x) in set(map(tuple,coords)):
                if not any(a['name']==f'reconstructed-virtual-{frame}-{y}-{x}' for a in anchors):
                    reference=next(ray['hit'] for ray in scene['frames'][frame]['rays'] if ray['name']==f'virtual-{coords.index([y,x])}')
                    error,_=matching(virtual,reference,[plan['scene_anchor_tolerance_m']])
                    anchors.append(dict(frame=frame,name=f'reconstructed-virtual-{frame}-{y}-{x}',point_error_m=error,
                        passed=virtual['kind']==reference['kind'] and error is not None and error<=plan['scene_anchor_tolerance_m']))
                previous=next(r for r in old['rows'] if (r['frame'],r['y'],r['x'])==(frame,y,x))
                historical=next(c for c in previous['cases'] if c['variant']==case['variant'])
                row['historical_eligible']=historical['eligible_cameras']
                key=(frame,y,x)
                if key in points_by_frame and not np.array_equal(q,points_by_frame[key]): raise ValueError('selected current carrier varies across conditions')
                points_by_frame[key]=q
                for s in sources:
                    key=(frame,y,x,s['camera']);uv=np.asarray(s['uv'],np.float32)
                    if key in uv_by_frame and not np.array_equal(uv,uv_by_frame[key]): raise ValueError('selected UV varies across conditions')
                    uv_by_frame[key]=uv
                primary.append(row)
            if (y,x) in set(grid): inventory.append(row)
        captures.append(dict(frame=frame,variant=case['variant'],report=str(meta_path.relative_to(root)),report_sha256=sha(meta_path),device=m['device'],source_fingerprint=m['source_fingerprint'],camera_roundtrip_error=camera_delta))
    all_rows=primary+inventory
    return dict(schema_version=1,scope=plan['scope'],plan_sha256=sha(path),invocation_sha256=sha(calls_path),
        scene_anchor_checks=anchors,all_scene_anchors_passed=all(a['passed'] for a in anchors),
        all_metric_oracles_passed=all(r['metric_oracle_passed'] for r in all_rows),
        captures=captures,primary=primary,inventory=inventory,limitations=plan['limitations'])


if __name__=='__main__':
    root=Path(__file__).resolve().parents[2]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=Path('configs/research/mesa-source-truth-plan.json'))
    parser.add_argument('--capture',action='store_true');parser.add_argument('--output-report',type=Path)
    args=parser.parse_args()
    if args.capture:capture(root,args.plan.resolve())
    if args.output_report:args.output_report.write_text(json.dumps(collect(root,args.plan.resolve()),indent=2,allow_nan=False)+'\n')
