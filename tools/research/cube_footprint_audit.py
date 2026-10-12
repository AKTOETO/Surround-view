"""Frozen cubemap-to-fisheye color reconstruction and independent ray support audit."""
import json
import numpy as np

# Independent explicit bases for the top-left cube pixel-center contract.
BASES={
 'px':np.array([[0,0,-1],[0,1,0],[1,0,0]]),
 'nx':np.array([[0,0,1],[0,1,0],[-1,0,0]]),
 'py':np.array([[1,0,0],[0,0,-1],[0,1,0]]),
 'ny':np.array([[1,0,0],[0,0,1],[0,-1,0]]),
 'pz':np.eye(3,dtype=int),
 'nz':np.array([[-1,0,0],[0,1,0],[0,0,-1]])}


def fisheye_ray(pixel,optics):
    if any(optics['k']) or optics.get('alpha',0):raise ValueError('zero-distortion equidistant required')
    a,b=(np.asarray(pixel,float)-[optics['cx'],optics['cy']])/[optics['fx'],optics['fy']]
    theta=np.hypot(a,b);scale=np.sin(theta)/theta if theta>1e-14 else 1.
    return np.array([a*scale,b*scale,np.cos(theta)])


def cube_sample(ray,size):
    ray=np.asarray(ray,float)
    if ray.shape!=(3,) or not np.isfinite(ray).all() or not np.any(ray) or size<2:raise ValueError('finite nonzero ray and size>=2 required')
    axis=int(np.argmax(np.abs(ray)));face=('px','py','pz')[axis] if ray[axis]>0 else ('nx','ny','nz')[axis]
    local=BASES[face]@ray
    return face,(local[:2]/local[2]+1)*size/2-.5


def cube_ray(face,pixel,size):
    xy=(np.asarray(pixel,float)+.5)*2/size-1
    ray=BASES[face].T@np.array([xy[0],xy[1],1.]);return ray/np.linalg.norm(ray)


def prepare(root,old_plan):
    from PIL import Image
    from source_correspondence import sha,verified
    from source_footprint_audit import footprint
    from displaced_correspondence import linearize,encode
    plan=json.loads(old_plan.read_text());prior=json.loads((root/'docs/validation/baselines/source_footprint_v1.json').read_text())
    capture_dir=root/plan['input_root']/'seed101-capture';capture=json.loads((capture_dir/'capture.json').read_text())
    manifest_path=root/plan['input_root']/'seed101-inputs/manifest.json';manifest=json.loads(manifest_path.read_text())
    size=capture['face_size'];requests={};groups=[];bound={};images={}
    for row in prior['rows']:
        frame,carrier=row['frame_index'],row['carrier'];d=root/plan['output_root']/f'frame{frame}'/carrier
        arrays={name:np.load(verified(d/name,digest),allow_pickle=False) for name,digest in row['scene_record']['files'].items()}
        support=((arrays['texel-kinds.npy']==1)&(arrays['weights.npy']>0)).any(axis=(1,2))
        indices=np.flatnonzero(arrays['mismatch.npy']&~support);refs=[]
        for n in indices:
            ids=[]
            for cam in range(4):
                for j,weight in enumerate(arrays['weights.npy'][n,cam]):
                    if weight<=0:continue
                    pixel=arrays['texels.npy'][n,cam,j];key=f'{frame}:{cam}:{pixel[0]}:{pixel[1]}'
                    ids.append(key)
                    if key in requests:continue
                    optics=capture['config']['cameras'][cam]['projection']
                    face,uv=cube_sample(fisheye_ray(pixel,optics),size)
                    xy,w=footprint(uv,size,size)
                    filename=capture['frames'][frame]['cameras'][cam]['faces'][face];path=capture_dir/filename
                    verified(path,capture['sha256'][filename]);bound[str(path.relative_to(root))]=sha(path)
                    if filename not in images:images[filename]=np.asarray(Image.open(path).convert('RGB'))
                    rgb=images[filename][xy[:,1],xy[:,0]]
                    predicted=np.rint(np.clip(encode((linearize(rgb/255)*w[:,None]).sum(axis=0)),0,1)*255).astype(np.uint8)
                    sourcepath=manifest_path.parent/manifest['frames'][frame]['paths'][cam]
                    verified(sourcepath,manifest['sha256'][sourcepath.name]);bound[str(sourcepath.relative_to(root))]=sha(sourcepath)
                    if str(sourcepath) not in images:images[str(sourcepath)]=np.asarray(Image.open(sourcepath).convert('RGB'))
                    actual=images[str(sourcepath)][pixel[1],pixel[0]]
                    if not np.array_equal(predicted,actual):raise ValueError('converter RGB reconstruction differs')
                    requests[key]={'frame':frame,'camera':cam,'pixel':pixel.tolist(),'face':face,'cube_uv':uv.tolist(),
                                   'texels':xy.tolist(),'weights':w.tolist(),'cube_rgb':rgb.tolist(),'source_rgb':actual.tolist(),
                                   'face_path':str(path.relative_to(root)),'source_path':str(sourcepath.relative_to(root))}
            refs.append({'pixel_yx':arrays['pixels.npy'][n].tolist(),'request_ids':sorted(set(ids))})
        groups.append({'frame':frame,'carrier':carrier,'residual_pixels':refs})
    return {'face_size':size,'requests':requests,'groups':groups,'bound_images':bound}


def collect(root,plan_path):
    from source_correspondence import sha,verified
    from displaced_correspondence import chroma_mask,linearize,encode
    from source_footprint_audit import footprint
    from PIL import Image
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    requests=json.loads((root/plan['requests']).read_text());scene_path=root/plan['output_root']/'scene-truth.json';scene=json.loads(scene_path.read_text())
    if scene['plan_sha256']!=sha(plan_path):raise ValueError('plan differs')
    verified(root/'tools/blender/cube_footprint.py',scene['helper_sha256'])
    if set(scene['requests'])!=set(requests['requests']):raise ValueError('request set differs')
    for name,digest in requests['bound_images'].items():verified(root/name,digest)
    request_rows={};images={}
    for key,q in requests['requests'].items():
        face,uv=cube_sample(fisheye_ray(q['pixel'],scene['cameras'][q['camera']]['projection']),requests['face_size'])
        xy,weights=footprint(uv,requests['face_size'],requests['face_size'])
        if face!=q['face'] or not np.array_equal(uv,q['cube_uv']) or not np.array_equal(xy,q['texels']) or not np.array_equal(weights,q['weights']):raise ValueError('cube mapping differs')
        for name in [q['face_path'],q['source_path']]:
            if name not in images:images[name]=np.asarray(Image.open(root/name).convert('RGB'))
        rgb=images[q['face_path']][xy[:,1],xy[:,0]]
        actual=images[q['source_path']][q['pixel'][1],q['pixel'][0]]
        predicted=np.rint(np.clip(encode((linearize(rgb/255)*weights[:,None]).sum(axis=0)),0,1)*255).astype(np.uint8)
        if not np.array_equal(rgb,q['cube_rgb']) or not np.array_equal(actual,q['source_rgb']) or not np.array_equal(predicted,actual):raise ValueError('actual color reconstruction differs')
        truth=scene['requests'][key];positive=weights>0
        if truth['source_center_kind']!=2:raise ValueError('old residual source texel center no longer road')
        target=np.asarray(truth['cube_center_kinds'])==1
        grid=np.asarray(truth['cube_grid_target_hits'])
        request_rows[key]={'center_support':bool((positive&target).any()),'grid_support':bool((positive&(grid>0)).any()),
                           'chroma_support':bool((positive&chroma_mask(rgb)).any()),'source_chroma':bool(chroma_mask(actual)),
                           'target_cube_weight':float((weights*target).sum()),'source_rgb':actual.tolist()}
    rows=[]
    for group in requests['groups']:
        counts={'pixels':len(group['residual_pixels']),'cube_center_target_support':0,'grid_only_target_support':0,
                'no_sampled_target_support':0,'positive_cube_chroma_support':0}
        for pixel in group['residual_pixels']:
            values=[request_rows[k] for k in pixel['request_ids']]
            center=any(v['center_support'] for v in values);grid=any(v['grid_support'] for v in values)
            counts['cube_center_target_support']+=int(center)
            counts['grid_only_target_support']+=int(not center and grid)
            counts['no_sampled_target_support']+=int(not center and not grid)
            counts['positive_cube_chroma_support']+=int(any(v['chroma_support'] for v in values))
        rows.append({'frame':group['frame'],'carrier':group['carrier'],**counts})
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'collector_sha256':sha(__file__),
            'scene_truth_sha256':sha(scene_path),'helper_sha256':scene['helper_sha256'],'blender_version':scene['blender_version'],
            'requests_sha256':sha(root/plan['requests']),'unique_source_texels':len(request_rows),'rgb_max_error_lsb':0,
            'rows':rows,'requests':request_rows,'limitations':plan['limitations']}
