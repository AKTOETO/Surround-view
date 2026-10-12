"""Matched native hybrid capture audit; local ray labels do not label nonlocal pyramid contributions."""
import json
import numpy as np
from scipy.ndimage import zoom

from source_correspondence import sha,verified,load_product,describe
from displaced_correspondence import chroma_mask,encode


def reconstruct_rgba(contributions, validity, fallback, ego=None):
    color=np.clip(np.sum(contributions,axis=2,dtype=np.float32),0,1)
    rgb=np.floor(np.clip(encode(color),0,1)*255+.5).astype(np.uint8)
    expected=fallback.copy();observed=validity.any(axis=2)
    expected[observed,:3]=rgb[observed]
    if ego is not None:expected[ego[...,3]!=0]=ego[ego[...,3]!=0]
    return expected


def resize_to(image,shape):
    factors=(shape[0]/image.shape[0],shape[1]/image.shape[1])+tuple(1 for _ in image.shape[2:])
    return zoom(image,factors,order=1)


def collect(root,plan_path):
    plan=json.loads(plan_path.read_text())
    for name,digest in plan['frozen_files'].items():verified(root/name,digest)
    output=root/plan['output_root'];inv_path=output/'invocations.json'
    inv=json.loads(inv_path.read_text());verified(output/'sv-render-fusion-probe',inv['binary_sha256'])
    if inv['binary_sha256']!=plan['binary_sha256'] or inv['plan_sha256']!=sha(plan_path) or len(inv['cases'])!=len(plan['frames'])*len(plan['carriers']) or any(c['returncode'] for c in inv['cases']):
        raise ValueError('capture invocation differs')
    displaced=json.loads((root/plan['displaced_baseline']).read_text())
    archived=json.loads((root/plan['truth_path']).read_text())
    rows=[]
    for frame in plan['frames']:
        reference=root/plan['reference_root']/f'frame{frame}'
        expected_mask=np.load(reference/'direct-mask.npy',allow_pickle=False)
        visibility_name=archived['frames'][frame]['visibility']
        visibility=np.load(verified(root/plan['input_root']/'seed101-capture'/visibility_name,archived['sha256'][visibility_name]),allow_pickle=False)
        expected_kinds={}
        for carrier in plan['carriers']:
            old_dir=reference/'native'/carrier;old_meta=json.loads((old_dir/'report.json').read_text())
            old_products={p['file']:load_product(old_dir,p) for p in old_meta['products']}
            index_dir=output/f'frame{frame}'/carrier;index_path=index_dir/'index.json';index=json.loads(index_path.read_text())
            if (index['frame_index']!=frame or index['scenario_timestamp_ns']!=old_meta['scenario_timestamp_ns']
                    or index['inputs']!=old_meta['inputs'] or index['config_sha256']!=old_meta['config_sha256']
                    or index['manifest_sha256']!=old_meta['manifest_sha256'] or not index['normal_output_unchanged']
                    or not index['color_only'] or index['device']!=old_meta['device'] or index['origin']!='top_left'):
                raise ValueError('paired native index differs')
            for source in index['inputs']:verified(root/source['path'],source['sha256'])
            h,w=index['height'],index['width']
            if (h,w)!=expected_mask.shape:raise ValueError('raster differs')
            dtype='<f4' if index['float_endian']=='little' else '>f4'
            direct_scene=json.loads((reference/'scene-truth.json').read_text())
            expected_kinds[carrier]=np.load(verified(reference/(carrier+'-source-kinds.npy'),direct_scene['files'][carrier+'-source-kinds.npy']),allow_pickle=False)
            old_proxy=chroma_mask(old_products['output.rgba'][...,:3],plan['chroma_threshold'])
            dr=next(r for r in displaced['rows'] if r['frame_index']==frame and r['carrier']==carrier)
            dr_dir=root/plan['displaced_root']/f'frame{frame}'/carrier
            displaced_kinds=np.load(verified(dr_dir/'source-kinds.npy',dr['scene_record']['files']['source-kinds.npy']),allow_pickle=False)
            rois={'expected_target':expected_mask,'source_visible_target':expected_mask&(visibility!=0),'fixed_edge_proxy':old_proxy}
            kinds_by_roi={'expected_target':expected_kinds[carrier],
                          'source_visible_target':expected_kinds[carrier][visibility[expected_mask]!=0],
                          'fixed_edge_proxy':displaced_kinds}
            cases=index['cases']
            if {(c['mode'],c['boundary']) for c in cases}!={tuple(c) for c in plan['variants']} or len(cases)!=len(plan['variants']):
                raise ValueError('fusion variants differ')
            for case in cases:
                directory=index_dir/case['directory']
                for name,descriptor in case['files'].items():
                    path=verified(directory/name,descriptor['sha256'])
                    if path.stat().st_size!=descriptor['bytes']:raise ValueError('payload bytes differ')
                def matrix(name,channels=1,height=h,width=w):
                    if name not in case['files']:raise ValueError('unbound native product')
                    values=np.fromfile(directory/name,np.uint8 if name.endswith(('.u8','.rgba')) else dtype)
                    return values.reshape(height,width,channels)
                regions=matrix('carrier-regions.u8')[...,0]
                positions=matrix('carrier-positions.f32',3)
                if not np.array_equal(regions,old_products['carrier-regions.u8'][...,0]) or not np.array_equal(positions,old_products['carrier-positions.f32'],equal_nan=True):
                    raise ValueError('hybrid carrier point differs from edge reference')
                validity=[];contributions=[]
                for i in range(4):
                    status=matrix(f'camera{i}-source-status.u8')[...,0]
                    uv=matrix(f'camera{i}-source-pixels.f32',2)
                    if not np.array_equal(status,old_products[f'camera{i}-status.u8'][...,0]) or not np.array_equal(uv,old_products[f'camera{i}-pixels.f32'],equal_nan=True):
                        raise ValueError('hybrid actual source projection differs from frozen rays')
                    validity.append(matrix(f'camera{i}-valid.u8')[...,0]!=0)
                    contributions.append(matrix(f'camera{i}-contribution.f32',3))
                validity=np.stack(validity,axis=2);contributions=np.stack(contributions,axis=2)
                weights=matrix('blend-weights.f32',4)
                actual=matrix('actual.rgba',4);fallback=matrix('fallback.rgba',4)
                if sha(directory/'fallback.rgba')!=sha(old_dir/'output.rgba'):
                    raise ValueError('hybrid fallback RGBA differs from edge baseline')
                ego=matrix('ego.rgba',4) if 'ego.rgba' in case['files'] else None
                recomposed=reconstruct_rgba(contributions,validity,fallback,ego)
                composition_error=int(np.abs(actual.astype(int)-recomposed.astype(int)).max())
                if composition_error>plan['rgba_tolerance_lsb']:raise ValueError('signed contributions do not compose native RGB')
                levels=[];bands=[]
                for number,level in enumerate(case['fusion_levels']):
                    lh,lw=level['height'],level['width']
                    lwgt=matrix(level['file'],4,lh,lw)
                    sums=lwgt.sum(axis=2);nonzero=sums>0
                    normalization=float(np.max(np.abs(sums[nonzero]-1))) if nonzero.any() else 0
                    if normalization>2e-6 or np.any(lwgt<0) or not np.isfinite(lwgt).all():raise ValueError('level coefficient normalization differs')
                    band=np.stack([matrix(n,3,lh,lw) for n in level['contributions']],axis=2)
                    if band.shape[2]!=4 or not np.isfinite(band).all():raise ValueError('signed band shape/value differs')
                    bands.append(band)
                    # Each band's contribution propagated through the actual level-size chain.
                    propagated=band
                    for finer in reversed(case['fusion_levels'][:number]):
                        propagated=resize_to(propagated,(finer['height'],finer['width']))
                    levels.append({'level':number,'height':lh,'width':lw,'normalized_weight_max_error':normalization,
                                   'negative_band_channel_values':int((band<0).sum()),
                                   'rois':{name:{'mean_abs_signed_linear_rgb':float(np.abs(propagated[roi]).mean()),
                                                 'max_abs_signed_linear_rgb':float(np.abs(propagated[roi]).max())}
                                           for name,roi in rois.items()}})
                reconstructed=bands[-1]
                for band in reversed(bands[:-1]):reconstructed=band+resize_to(reconstructed,band.shape[:2])
                band_error=float(np.max(np.abs(reconstructed-contributions)))
                if band_error>plan['band_tolerance_linear_rgb']:raise ValueError('SciPy band reconstruction differs')
                predicted=chroma_mask(actual[...,:3],plan['chroma_threshold'])
                magnitude=np.max(np.abs(contributions),axis=3)
                table={}
                for name,roi in rois.items():
                    kinds=kinds_by_roi[name];local_target=kinds==1
                    local_mass=(weights[roi]*local_target).sum(axis=1)
                    nonlocal_pairs=(~validity[roi])&(magnitude[roi]>plan['contribution_epsilon'])
                    zero_weight=(weights[roi]<=plan['contribution_epsilon'])&(magnitude[roi]>plan['contribution_epsilon'])
                    table[name]={'pixels':int(roi.sum()),'proxy_pixels':int(predicted[roi].sum()),
                                 'local_target_ray_weight_mass':describe(local_mass),
                                 'nonlocal_invalid_camera_pairs':int(nonlocal_pairs.sum()),
                                 'pixels_with_nonlocal_invalid_camera':int(nonlocal_pairs.any(axis=1).sum()),
                                 'nonlocal_invalid_max_abs_linear_rgb':float(np.max(magnitude[roi][~validity[roi]])) if (~validity[roi]).any() else 0,
                                 'zero_local_weight_contributing_camera_pairs':int(zero_weight.sum()),
                                 'signed_contribution_min':float(contributions[roi].min()),'signed_contribution_max':float(contributions[roi].max())}
                rows.append({'frame_index':frame,'carrier':carrier,'mode':case['mode'],'boundary':case['boundary'],
                             'settings':case['settings'],'index_sha256':sha(index_path),'device':index['device'],
                             'source_revision':index['source_revision'],'source_fingerprint':index['source_fingerprint'],
                             'directory':str(directory.relative_to(root)),'files':case['files'],
                             'source_and_carrier_equal_to_edge':True,'fallback_equal_to_edge':True,
                             'max_composition_error_lsb':composition_error,'max_band_reconstruction_error_linear_rgb':band_error,
                             'full_proxy_pixels':int(predicted.sum()),
                             'proxy_pixels_outside_existing_ray_truth':int((predicted&~old_proxy&~expected_mask).sum()),
                             'rois':table,'levels':levels})
    return {'schema_version':1,'scope':plan['scope'],'plan_sha256':sha(plan_path),'collector_sha256':sha(__file__),
            'binary_sha256':inv['binary_sha256'],'probe_source_sha256':sha(root/'tests/render_fusion_probe.cpp'),
            'invocations_sha256':sha(inv_path),'rows':rows,'limitations':plan['limitations']}
