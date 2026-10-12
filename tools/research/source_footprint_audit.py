"""Independent four-texel audit of frozen point-ray/RGB mismatches; no product renderer."""
import json
import numpy as np
from PIL import Image
from source_correspondence import sha, verified, load_product, describe
from displaced_correspondence import bilinear_rgb, chroma_mask


def footprint(pixels, width, height):
    """Top-left integer pixel centers, GL clamp-to-edge, ordered TL/TR/BL/BR."""
    p=np.asarray(pixels,dtype=float)
    if p.shape[-1]!=2 or not np.isfinite(p).all() or width<1 or height<1:
        raise ValueError('finite xy coordinates and positive raster required')
    base=np.floor(p).astype(np.int64);f=p-base;x,y=f[...,0],f[...,1]
    xy=base[...,None,:]+np.array([[0,0],[1,0],[0,1],[1,1]])
    xy[...,0]=np.clip(xy[...,0],0,width-1);xy[...,1]=np.clip(xy[...,1],0,height-1)
    weights=np.stack([(1-x)*(1-y),x*(1-y),(1-x)*y,x*y],axis=-1)
    return xy,weights


def selected_pixels(kinds,status,control_count):
    target=((kinds==1)&(status==1)).any(axis=1)
    controls=np.flatnonzero(target)[:control_count]
    ids=np.sort(np.concatenate([np.flatnonzero(~target),controls]))
    return ids,~target[ids]


def collect(root,plan_path):
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    output=root/plan['output_root'];scene_path=output/'scene-truth.json'
    scene=json.loads(scene_path.read_text())
    if scene['plan_sha256']!=sha(plan_path):raise ValueError('scene plan differs')
    verified(root/'tools/blender/source_footprint.py',scene['helper_sha256'])
    prior=json.loads((root/plan['displaced_baseline']).read_text());rows=[]
    for record,old in zip(scene['records'],prior['rows'],strict=True):
        frame,carrier=record['frame_index'],record['carrier']
        if (frame,carrier)!=(old['frame_index'],old['carrier']):raise ValueError('case order differs')
        native=root/plan['native_root']/f'frame{frame}'/'native'/carrier
        meta=json.loads(verified(native/'report.json',old['native_report_sha256']).read_text())
        products={p['file']:load_product(native,p) for p in meta['products']}
        previous=root/plan['displaced_root']/f'frame{frame}'/carrier
        old_arrays={name:np.load(verified(previous/name,digest),allow_pickle=False) for name,digest in old['scene_record']['files'].items()}
        pixels=old_arrays['pixels.npy'];y,x=pixels.T
        status=np.stack([products[f'camera{i}-status.u8'][y,x,0] for i in range(4)],axis=1)
        indices,mismatch=selected_pixels(old_arrays['source-kinds.npy'],status,plan['control_pixels_per_case'])
        coords=pixels[indices];y,x=coords.T;status=status[indices]
        directory=output/f'frame{frame}'/carrier
        arrays={name:np.load(verified(directory/name,digest),allow_pickle=False) for name,digest in record['files'].items()}
        if not np.array_equal(arrays['pixels.npy'],coords) or not np.array_equal(arrays['mismatch.npy'],mismatch):raise ValueError('selection differs')
        if not np.array_equal(arrays['center-kinds.npy'],old_arrays['source-kinds.npy'][indices]):raise ValueError('reconstructed center identity differs')
        if not np.allclose(arrays['center-hits.npy'],old_arrays['source-hits.npy'][indices],atol=1e-5,rtol=0,equal_nan=True):raise ValueError('reconstructed center hit differs')
        supported=np.zeros(len(coords),bool);positive_rgb=np.zeros(len(coords),bool);pair_rows=[]
        for i,camera in enumerate(meta['config']['cameras']):
            valid=status[:,i]==1;source=meta['inputs'][i]
            image=np.asarray(Image.open(verified(root/source['path'],source['sha256'])).convert('RGB'))
            xy,weights=footprint(products[f'camera{i}-pixels.f32'][y,x][valid],image.shape[1],image.shape[0])
            if not np.array_equal(xy,arrays['texels.npy'][valid,i]) or not np.array_equal(weights,arrays['weights.npy'][valid,i]):raise ValueError('footprint differs')
            texel_rgb=image[xy[...,1],xy[...,0]]
            reconstructed=(texel_rgb/255*weights[...,None]).sum(axis=1)
            np.testing.assert_allclose(reconstructed,bilinear_rgb(image,products[f'camera{i}-pixels.f32'][y,x][valid]),rtol=0,atol=2e-15)
            target=(arrays['texel-kinds.npy'][valid,i]==1)&(weights>0)
            colored=chroma_mask(texel_rgb)&(weights>0)
            supported[valid]|=target.any(axis=1);positive_rgb[valid]|=colored.any(axis=1)
            pair_rows.append({'camera_id':i,'valid_pairs':int(valid.sum()),'pairs_with_positive_target_texel':int(target.any(axis=1).sum()),
                              'pairs_with_positive_chroma_texel':int(colored.any(axis=1).sum()),
                              'target_texel_weight_mass':describe((target*weights).sum(axis=1))})
        def counts(mask):
            return {'pixels':int(mask.sum()),'positive_weight_target_texel_pixels':int((mask&supported).sum()),
                    'no_target_center_texel_pixels':int((mask&~supported).sum()),
                    'positive_weight_chroma_texel_pixels':int((mask&positive_rgb).sum()),
                    'chroma_texel_without_target_center_pixels':int((mask&positive_rgb&~supported).sum())}
        rows.append({'frame_index':frame,'carrier':carrier,'native_report_sha256':old['native_report_sha256'],
                     'scene_record':record,'mismatch':counts(mismatch),'control':counts(~mismatch),'pairs':pair_rows})
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'collector_sha256':sha(__file__),
            'scene_truth_sha256':sha(scene_path),'helper_sha256':scene['helper_sha256'],'blender_version':scene['blender_version'],
            'rows':rows,'limitations':plan['limitations']}
