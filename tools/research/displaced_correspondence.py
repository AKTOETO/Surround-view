"""Read-only displaced coded-color attribution; point-ray identity is not bilinear object identity."""
import json
import numpy as np
from PIL import Image

from source_correspondence import sha,verified,load_product,describe


def chroma_mask(rgb, threshold=.15):
    rgb=np.asarray(rgb,float)/255
    return np.minimum(rgb[...,0],rgb[...,2])-rgb[...,1]>threshold


def bilinear_rgb(image, pixels):
    image=np.asarray(image,float)/255;pixels=np.asarray(pixels,float)
    if not np.isfinite(pixels).all():raise ValueError('finite sample coordinates required')
    base=np.floor(pixels).astype(int);fraction=pixels-base
    h,w=image.shape[:2];x,y=base.T;fx,fy=fraction.T
    return (image[np.clip(y,0,h-1),np.clip(x,0,w-1)]*((1-fx)*(1-fy))[:,None]
            +image[np.clip(y,0,h-1),np.clip(x+1,0,w-1)]*(fx*(1-fy))[:,None]
            +image[np.clip(y+1,0,h-1),np.clip(x,0,w-1)]*((1-fx)*fy)[:,None]
            +image[np.clip(y+1,0,h-1),np.clip(x+1,0,w-1)]*(fx*fy)[:,None])


def linearize(rgb):
    return np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)


def encode(rgb):
    return np.where(rgb<=.0031308,rgb*12.92,1.055*np.maximum(rgb,0)**(1/2.4)-.055)


def collect(root,plan_path):
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    verified(root/plan['binary_path'],plan['binary_sha256'])
    output=root/plan['output_root'];scene_path=output/'scene-truth.json'
    scene=json.loads(scene_path.read_text())
    if scene['plan_sha256']!=sha(plan_path):raise ValueError('scene plan differs')
    verified(root/'tools/blender/displaced_correspondence.py',scene['helper_sha256'])
    rows=[]
    for record in scene['records']:
        frame,carrier=record['frame_index'],record['carrier']
        native=root/plan['native_root']/f'frame{frame}'/'native'/carrier
        metadata=json.loads(verified(native/'report.json',record['native_report_sha256']).read_text())
        if metadata['frame_index']!=frame or not metadata['normal_output_unchanged']:
            raise ValueError('native capture differs')
        products={p['file']:load_product(native,p) for p in metadata['products']}
        rgb=products['output.rgba'][...,:3]
        selected=chroma_mask(rgb,plan['chroma_threshold'])
        directory=output/f'frame{frame}'/carrier
        arrays={name:np.load(verified(directory/name,digest),allow_pickle=False) for name,digest in record['files'].items()}
        coordinates=arrays['pixels.npy'];y,x=coordinates.T
        if not np.array_equal(coordinates,np.argwhere(selected)):
            raise ValueError('RGB-conditioned pixel ordering differs')
        direct_mask=np.load(root/plan['native_root']/f'frame{frame}'/'direct-mask.npy',allow_pickle=False)
        status=np.stack([products[f'camera{i}-status.u8'][y,x,0] for i in range(4)],axis=-1)
        kinds=arrays['source-kinds.npy'];virtual_kinds=arrays['virtual-kinds.npy']
        weights=np.zeros(status.shape);colors=np.zeros((*status.shape,3))
        for i,camera in enumerate(metadata['config']['cameras']):
            valid=status[:,i]==1;pixels=products[f'camera{i}-pixels.f32'][y,x][valid]
            source=metadata['inputs'][i]
            if source['camera_id']!=i:raise ValueError('camera order differs')
            image=np.asarray(Image.open(verified(root/source['path'],source['sha256'])).convert('RGB'))
            if image.shape[:2]!=(camera['resolution']['height'],camera['resolution']['width']):
                raise ValueError('input size differs')
            colors[valid,i]=bilinear_rgb(image,pixels)
            edge=np.minimum(np.minimum(pixels[:,0],pixels[:,1]),
                            np.minimum(image.shape[1]-1-pixels[:,0],image.shape[0]-1-pixels[:,1]))
            width=metadata['config']['fusion']['edge_width_px']
            weights[valid,i]=np.clip(edge/width,0,1)
        total=weights.sum(axis=1)
        if np.any(total<=1e-6):raise ValueError('chroma proxy has no weighted sample')
        weights/=total[:,None]
        predicted=encode((linearize(colors)*weights[...,None]).sum(axis=1))*255
        error=np.abs(predicted-rgb[y,x])
        source_target=(kinds==1)&(status==1)
        target_mass=(weights*source_target).sum(axis=1)
        hits=arrays['source-hits.npy'];direct=arrays['virtual-points.npy']
        distances=np.linalg.norm(hits-direct[:,None,:],axis=-1)
        same=(kinds==virtual_kinds[:,None])&(status==1)&(kinds!=0)
        matched=same&(distances<=plan['surface_match_tolerance_m'])
        near_match=(matched*weights).sum(axis=1)
        displaced=~direct_mask[y,x]
        sub={}
        for name,roi in [('all_proxy',np.ones(len(y),bool)),('displaced_proxy',displaced)]:
            sub[name]={'pixels':int(roi.sum()),'valid_source_pairs':int((status[roi]==1).sum()),
                       'virtual_kind_counts':{str(k):int((virtual_kinds[roi]==k).sum()) for k in np.unique(virtual_kinds[roi])},
                       'source_kind_counts':{str(k):int(((kinds==k)&(status==1))[roi].sum()) for k in np.unique(kinds[status==1])},
                       'pixels_with_target_source_ray':int(source_target[roi].any(axis=1).sum()),
                       'target_ray_weight_mass':describe(target_mass[roi]),
                       'surface_match_weight_mass':describe(near_match[roi]),
                       'source_target_to_virtual_distance_m':describe(distances[roi][source_target[roi]]),
                       'bilinear_rgb_reconstruction_abs_lsb':describe(error[roi].ravel())}
        rows.append({'frame_index':frame,'carrier':carrier,'native_report_sha256':record['native_report_sha256'],
                     'scene_record':record,'proxy_pixels':len(y),'expected_target_overlap_pixels':int((~displaced).sum()),
                     'device':metadata['device'],'output_sha256':sha(native/'output.rgba'),'rois':sub})
    expected=[(f,c) for f in plan['frames'] for c in plan['carriers']]
    if [(r['frame_index'],r['carrier']) for r in rows]!=expected:raise ValueError('capture cases differ')
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'collector_sha256':sha(__file__),
            'helper_sha256':scene['helper_sha256'],'scene_truth_sha256':sha(scene_path),'blender_version':scene['blender_version'],
            'binary_sha256':plan['binary_sha256'],'rows':rows,'limitations':plan['limitations']}
