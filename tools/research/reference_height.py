"""Read-only assumed-height ablation; the height is supplied, never estimated from RGB."""
import json
import numpy as np
from PIL import Image
from source_correspondence import sha,verified,load_product,describe,project
from source_correspondence_controls import transform_points
from displaced_correspondence import chroma_mask,linearize


def mask_scores(predicted,expected):
    intersection=int((predicted&expected).sum());union=int((predicted|expected).sum())
    return {'expected_pixels':int(expected.sum()),'proxy_pixels':int(predicted.sum()),'intersection_pixels':intersection,
            'outside_expected_pixels':int((predicted&~expected).sum()),'raw_iou':intersection/union if union else None,
            'raw_recall':intersection/int(expected.sum()) if expected.any() else None}


def collect(root,plan_path):
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    output=root/plan['output_root'];inv_path=output/'invocations.json';inv=json.loads(inv_path.read_text())
    if inv['plan_sha256']!=sha(plan_path) or inv['binary_sha256']!=plan['binary_sha256']:raise ValueError('capture binding differs')
    verified(output/'sv-correspondence-probe',plan['binary_sha256'])
    truth=json.loads((root/plan['truth_path']).read_text());capture=(root/plan['truth_path']).parent
    target_id=next(v for k,v in truth['objects'].items() if 'coded object target' in k)
    road_id=next(v for k,v in truth['objects'].items() if k.startswith('SV road.'))
    rows=[];expected_order=[(f,h) for f in plan['frames'] for h in plan['heights_m']]
    if [(q['frame'],q['height']) for q in inv['cases']]!=expected_order or any(q['returncode'] for q in inv['cases']):raise ValueError('capture cases differ')
    for call in inv['cases']:
        frame,height=call['frame'],call['height'];name=call['name'];d=output/name
        meta=json.loads((d/'report.json').read_text());cfg=root/plan['config_root']/(name+'.json')
        if meta['config_sha256']!=sha(cfg) or meta['frame_index']!=frame or not meta['normal_output_unchanged']:raise ValueError('native capture differs')
        if meta['manifest_sha256']!=plan['manifest_sha256']:raise ValueError('manifest differs')
        for source in meta['inputs']:verified(root/source['path'],source['sha256'])
        products={p['file']:load_product(d,p) for p in meta['products']};rgb=products['output.rgba'][...,:3]
        t=truth['frames'][frame]
        ids=np.load(verified(capture/t['objects'],truth['sha256'][t['objects']]),allow_pickle=False)
        direct=np.asarray(Image.open(verified(capture/t['rgb'],truth['sha256'][t['rgb']])).convert('RGB'))
        vis=np.load(verified(capture/t['visibility'],truth['sha256'][t['visibility']]),allow_pickle=False)
        expected=ids==target_id;road=ids==road_id;visible=expected&(vis!=0)
        proxy=chroma_mask(rgb,plan['chroma_threshold']);metrics=mask_scores(proxy,expected)
        metrics['visible_target_proxy_hits']=int((proxy&visible).sum());metrics['visible_target_pixels']=int(visible.sum())
        regions=products['carrier-regions.u8'][...,0];positions=products['carrier-positions.f32']
        covered=(regions!=0)&(regions!=5)
        points=np.load(root/plan['reference_root']/f'frame{frame}'/'direct-points.npy',allow_pickle=False)
        world=transform_points(np.asarray(t['T_world_from_vehicle']),positions[expected&covered])
        metrics['carrier_to_virtual_target_distance_m']=describe(np.linalg.norm(world-points[expected&covered],axis=1))
        metrics['target_carrier_coverage_pixels']=int((expected&covered).sum())
        errors=[];ego=np.asarray(t['T_world_from_vehicle'])
        vehicle=transform_points(np.linalg.inv(ego),points[expected])
        y,x=np.argwhere(expected).T
        for i,camera in enumerate(meta['config']['cameras']):
            truth_uv,valid=project(camera,vehicle)
            actual=products[f'camera{i}-pixels.f32'][y,x];status=products[f'camera{i}-status.u8'][y,x,0]
            chosen=valid&(status==1)&((vis[expected]&(1<<i))!=0)
            errors.extend(np.linalg.norm(actual[chosen]-truth_uv[chosen],axis=1))
        metrics['visible_point_projection_error_px']=describe(errors);metrics['visible_point_projection_pairs']=len(errors)
        metrics['road_linear_rgb_mae']=float(np.abs(linearize(rgb[road]/255)-linearize(direct[road]/255)).mean())
        metrics['road_pixels']=int(road.sum());metrics['road_carrier_coverage_pixels']=int((road&covered).sum())
        old=root/plan['reference_root']/f'frame{frame}'/'native/plane/output.rgba'
        if height==0 and sha(d/'output.rgba')!=sha(old):raise ValueError('zero-height baseline RGBA differs')
        rows.append({'frame':frame,'height_m':height,'directory':str(d.relative_to(root)),
                     'source_revision':meta['source_revision'],'source_fingerprint':meta['source_fingerprint'],'device':meta['device'],
                     'report_sha256':sha(d/'report.json'),'output_sha256':sha(d/'output.rgba'),**metrics})
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'collector_sha256':sha(__file__),
            'binary_sha256':plan['binary_sha256'],'invocations_sha256':sha(inv_path),'rows':rows,'limitations':plan['limitations']}
