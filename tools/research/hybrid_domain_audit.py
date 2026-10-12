"""Independent scene attribution on all new hybrid chroma coordinates; not segmentation or recall."""
import json
import numpy as np


def new_domain(proxy,old_proxy,expected):
    if proxy.shape!=old_proxy.shape or proxy.shape!=expected.shape:raise ValueError('mask shapes differ')
    return proxy&~old_proxy&~expected


def support_metrics(kinds,texel_kinds,texel_weights,valid,weights,contributions,epsilon):
    center=(kinds==1)&valid
    texel=((texel_kinds==1)&(texel_weights>0)).any(axis=2)&valid
    magnitude=np.abs(contributions).max(axis=2)
    nonlocal_pairs=(~valid)&(magnitude>epsilon)
    zero_weight=(weights<=epsilon)&(magnitude>epsilon)
    return {'pixels':len(kinds),'local_target_ray_pixels':int(center.any(axis=1).sum()),
            'positive_local_fusion_target_ray_pixels':int((center&(weights>epsilon)).any(axis=1).sum()),
            'source_target_texel_pixels':int(texel.any(axis=1).sum()),
            'no_center_but_target_texel_pixels':int((~center.any(axis=1)&texel.any(axis=1)).sum()),
            'no_target_center_or_texel_pixels':int((~center.any(axis=1)&~texel.any(axis=1)).sum()),
            'local_target_ray_weight_sum':float((weights*center).sum()),
            'nonlocal_invalid_camera_pixels':int(nonlocal_pairs.any(axis=1).sum()),
            'nonlocal_invalid_camera_pairs':int(nonlocal_pairs.sum()),
            'zero_local_weight_contributing_pixels':int(zero_weight.any(axis=1).sum()),
            'nonlocal_invalid_max_abs_linear_rgb':float(magnitude[~valid].max()) if (~valid).any() else 0.}


def read_native(directory,case,index,name,channels):
    from source_correspondence import verified
    descriptor=case['files'][name];path=verified(directory/name,descriptor['sha256'])
    if path.stat().st_size!=descriptor['bytes']:raise ValueError('payload bytes differ')
    dtype=np.uint8 if name.endswith(('.u8','.rgba')) else ('<f4' if index['float_endian']=='little' else '>f4')
    return np.fromfile(path,dtype).reshape(index['height'],index['width'],channels)


def prepare(root,plan):
    from source_correspondence import verified,sha,load_product
    from displaced_correspondence import chroma_mask
    baseline=json.loads((root/plan['hybrid_baseline']).read_text());groups=[]
    for frame in plan['frames']:
        reference=root/plan['reference_root']/f'frame{frame}'
        expected=np.load(reference/'direct-mask.npy',allow_pickle=False)
        for carrier in plan['carriers']:
            old_dir=reference/'native'/carrier;old=json.loads((old_dir/'report.json').read_text())
            products={p['file']:p for p in old['products']}
            old_proxy=chroma_mask(load_product(old_dir,products['output.rgba'])[...,:3],plan['chroma_threshold'])
            index_dir=root/plan['hybrid_root']/f'frame{frame}'/carrier;index_path=index_dir/'index.json'
            bound=next(r for r in baseline['rows'] if r['frame_index']==frame and r['carrier']==carrier)
            index=json.loads(verified(index_path,bound['index_sha256']).read_text())
            if index['config_sha256']!=old['config_sha256'] or index['inputs']!=old['inputs']:raise ValueError('paired input/config differs')
            masks=[];cases=[]
            for case in index['cases']:
                directory=index_dir/case['directory']
                rgb=read_native(directory,case,index,'actual.rgba',4)
                selected=new_domain(chroma_mask(rgb[...,:3],plan['chroma_threshold']),old_proxy,expected)
                row=next(r for r in baseline['rows'] if r['frame_index']==frame and r['carrier']==carrier and r['mode']==case['mode'] and r['boundary']==case['boundary'])
                if selected.sum()!=row['proxy_pixels_outside_existing_ray_truth']:raise ValueError('new-domain count differs')
                masks.append(selected);cases.append(case)
            union=np.logical_or.reduce(masks);coords=np.argwhere(union);y,x=coords.T
            status=np.stack([load_product(old_dir,products[f'camera{i}-status.u8'])[y,x,0] for i in range(4)],axis=1)
            uv=np.stack([load_product(old_dir,products[f'camera{i}-pixels.f32'])[y,x] for i in range(4)],axis=1)
            positions=load_product(old_dir,products['carrier-positions.f32'])[y,x]
            for case in cases:
                directory=index_dir/case['directory']
                for i in range(4):
                    if not np.array_equal(read_native(directory,case,index,f'camera{i}-source-status.u8',1)[y,x,0],status[:,i]):raise ValueError('actual validity differs')
                    if not np.array_equal(read_native(directory,case,index,f'camera{i}-source-pixels.f32',2)[y,x],uv[:,i],equal_nan=True):raise ValueError('actual UV differs')
                if not np.array_equal(read_native(directory,case,index,'carrier-positions.f32',3)[y,x],positions,equal_nan=True):raise ValueError('actual carrier differs')
            if not np.isfinite(uv[status==1]).all() or not np.isfinite(positions).all():raise ValueError('finite valid geometry required')
            groups.append({'frame':frame,'carrier':carrier,'index_path':str(index_path.relative_to(root)),'index_sha256':sha(index_path),
                           'native_report_sha256':sha(old_dir/'report.json'),'pixels_yx':coords.tolist(),
                           'status':status.tolist(),'uv':np.where((status==1)[...,None],uv,0).tolist(),'carrier_points':positions.tolist(),
                           'cases':[{'mode':c['mode'],'boundary':c['boundary'],'directory':c['directory'],
                                     'union_indices':np.flatnonzero(mask[y,x]).tolist()} for c,mask in zip(cases,masks,strict=True)]})
    return {'groups':groups}


def collect(root,plan_path):
    from source_correspondence import sha,verified,describe
    from source_footprint_audit import footprint
    from source_correspondence_controls import transform_points
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    requests=json.loads((root/plan['requests']).read_text())
    if prepare(root,plan)!=requests:raise ValueError('request regeneration differs')
    scene_path=root/plan['output_root']/'scene-truth.json';scene=json.loads(scene_path.read_text())
    if scene['plan_sha256']!=sha(plan_path):raise ValueError('scene plan differs')
    verified(root/'tools/blender/hybrid_domain.py',scene['helper_sha256'])
    if len(scene['groups'])!=len(requests['groups']):raise ValueError('scene groups differ')
    truth=json.loads((root/plan['truth_path']).read_text());rows=[]
    for group,record in zip(requests['groups'],scene['groups'],strict=True):
        if (record['frame'],record['carrier'])!=(group['frame'],group['carrier']):raise ValueError('scene group order differs')
        d=root/plan['output_root']/f"frame{group['frame']}"/group['carrier']
        a={name:np.load(verified(d/name,digest),allow_pickle=False) for name,digest in record['files'].items()}
        if not np.array_equal(a['pixels.npy'],group['pixels_yx']):raise ValueError('scene coordinates differ')
        valid=np.asarray(group['status'])==1;uv=np.asarray(group['uv']);y,x=np.asarray(group['pixels_yx']).T
        index_path=root/group['index_path'];index=json.loads(verified(index_path,group['index_sha256']).read_text())
        for i,c in enumerate(index['config']['cameras']):
            xy,w=footprint(uv[valid[:,i],i],c['resolution']['width'],c['resolution']['height'])
            if not np.array_equal(a['texels.npy'][valid[:,i],i],xy) or not np.array_equal(a['texel-weights.npy'][valid[:,i],i],w):raise ValueError('footprint differs')
        pose=np.asarray(truth['frames'][group['frame']]['T_world_from_vehicle'])
        carrier_world=transform_points(pose,np.asarray(group['carrier_points']))
        carrier_distance=np.linalg.norm(a['source-hits.npy']-carrier_world[:,None,:],axis=2)
        virtual_distance=np.linalg.norm(a['source-hits.npy']-a['virtual-hits.npy'][:,None,:],axis=2)
        for selection in group['cases']:
            ids=np.asarray(selection['union_indices'],int);case=next(c for c in index['cases'] if c['directory']==selection['directory'])
            directory=index_path.parent/case['directory']
            weights=read_native(directory,case,index,'blend-weights.f32',4)[y,x][ids]
            contributions=np.stack([read_native(directory,case,index,f'camera{i}-contribution.f32',3)[y,x][ids] for i in range(4)],axis=1)
            metrics=support_metrics(a['source-kinds.npy'][ids],a['texel-kinds.npy'][ids],a['texel-weights.npy'][ids],valid[ids],weights,contributions,plan['contribution_epsilon'])
            target=(a['source-kinds.npy'][ids]==1)&valid[ids]
            same=(a['source-kinds.npy'][ids]==a['virtual-kinds.npy'][ids,None])&valid[ids]&(a['source-kinds.npy'][ids]!=0)
            metrics.update({'virtual_kind_counts':{str(int(k)):int((a['virtual-kinds.npy'][ids]==k).sum()) for k in np.unique(a['virtual-kinds.npy'][ids])},
                            'source_target_to_carrier_distance_m':describe(carrier_distance[ids][target]),
                            'source_target_to_virtual_distance_m':describe(virtual_distance[ids][target]),
                            'virtual_surface_match_pairs':int((same&(virtual_distance[ids]<=plan['surface_match_tolerance_m'])).sum())})
            rows.append({'frame':group['frame'],'carrier':group['carrier'],'mode':selection['mode'],'boundary':selection['boundary'],**metrics})
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'collector_sha256':sha(__file__),
            'helper_sha256':scene['helper_sha256'],'scene_truth_sha256':sha(scene_path),'blender_version':scene['blender_version'],
            'requests_sha256':sha(root/plan['requests']),'unique_frame_carrier_pixels':sum(len(g['pixels_yx']) for g in requests['groups']),
            'rows':rows,'limitations':plan['limitations']}
