"""Paired output-resolution audit; independent Blender truth, no product algorithms."""
import copy
import hashlib
import json
from pathlib import Path

import numpy as np

from object_metrics import measure_target, measure_visible_target, target_mask
from temporal_seam_stability import load_sequence, _verified

ROOT = Path(__file__).resolve().parents[2]


def validate_output_design(low_plan, high_plan, low_asset, high_asset):
    plans = copy.deepcopy([low_plan, high_plan])
    plans[1].pop('reference')
    for plan, dimensions in zip(plans, ([320,180],[640,360])):
        if plan.pop('output') != dimensions:
            raise ValueError('output experiment requires exactly 320x180 and 640x360')
        for key in ('experiment','inputs_plan','protocol'):
            plan.pop(key)
    assets = copy.deepcopy([low_asset, high_asset])
    from parallax_height import validate_factors
    for asset, dimensions in zip(assets, ((320,180),(640,360))):
        validate_factors(asset)
        for case in asset['cases']:
            capture = case['capture']
            if (capture.pop('width'),capture.pop('height')) != dimensions or capture['face_size'] != 512:
                raise ValueError('only output dimensions may change, with fixed cubemap512')
    if plans[0] != plans[1] or assets[0] != assets[1]:
        raise ValueError('output experiment changed a non-output factor')


def normalized_config(config, dimensions):
    result = copy.deepcopy(config)
    if result.pop('output') != dict(zip(('width','height'),dimensions)):
        raise ValueError('actual output dimensions differ from design')
    if any(c['resolution'] != {'width':400,'height':400} for c in result['cameras']):
        raise ValueError('fisheye dimensions changed')
    return result


def normalized_metrics(row, height):
    if type(height) is not int or height <= 0:
        raise ValueError('positive integer output height required')
    distance = row['object_position']['centroid_distance_px']
    return {'full_iou':row['quality']['target']['iou'],
            'conditional_iou':row['visible_target']['conditional_iou'],
            'predicted_area_fraction':row['quality']['target']['predicted_pixels']/(height*height*16/9),
            'centroid_distance_over_height':distance/height if distance is not None else None,
            'linear_mae':row['quality']['linear_mae']}


def deltas(low, high):
    return {k:high[k]-low[k] if low[k] is not None and high[k] is not None else None for k in low}


def output_followup(current, high_root, high_inputs):
    from parallax_height import audit, sha, verify_historical_results, sampling_rows, object_position, box_frame_bounds
    reference = current['plan']['reference']
    low_root, low_inputs = ROOT/reference['root'], ROOT/reference['inputs']
    historical = ROOT/reference['summary']
    if sha(historical) != reference['summary_sha256']:
        raise ValueError('frozen reference summary checksum mismatch')
    low = audit(ROOT/reference['plan'],low_root,low_inputs)
    verify_historical_results(low,json.loads(historical.read_text()))
    validate_output_design(low['plan'],current['plan'],low['asset'],current['asset'])
    if (low['source_fingerprint'] != current['source_fingerprint'] or low['render_device'] != current['render_device']):
        raise ValueError('output experiment implementation/device changed')
    controls, scaled_rows = [], {}
    from PIL import Image
    for case in low['asset']['cases']:
        cid,seed = case['id'],case['scenario']['seed']
        contexts, metadata, source_digests, truth_controls, binaries = [],[],[],[],[]
        cube_digests, cube_metadata, face_counts, file_hashes = [],[],[],[]
        for level,root,inputs,dimensions in (('low',low_root,low_inputs,(320,180)),
                                            ('high',high_root,high_inputs,(640,360))):
            capture = inputs/cid/f'seed{seed}-capture'
            cfg, images, direct, _, stamps, poses = load_sequence(inputs/cid/f'seed{seed}-inputs',capture,'any')
            contexts.append((normalized_config(cfg,dimensions),stamps,poses))
            info = json.loads((capture/'capture.json').read_text())
            normalized = copy.deepcopy(info)
            normalized['config'] = normalized_config(normalized['config'],dimensions)
            file_hashes.append(normalized.pop('sha256'))
            metadata.append(normalized)
            cube_digest = hashlib.sha256()
            profiles, faces = [], 0
            for frame in info['frames']:
                for camera in frame['cameras']:
                    for face,name in sorted(camera['faces'].items()):
                        with Image.open(_verified(capture,name,info['sha256'])) as image:
                            if image.size != (512,512) or image.mode != 'RGB':
                                raise ValueError('actual cube dimensions/mode changed')
                            cube_digest.update(f'{faces}:{camera["id"]}:{face}'.encode())
                            cube_digest.update(np.asarray(image).tobytes())
                            profiles.append({k:v for k,v in image.info.items()
                                             if k not in ('Date','Camera','Scene','RenderTime')})
                            faces += 1
            cube_digests.append(cube_digest.hexdigest());cube_metadata.append(profiles);face_counts.append(faces)
            digest = hashlib.sha256(np.asarray(images).tobytes()).hexdigest()
            source_digests.append(digest)
            truth = json.loads((capture/'paired_truth.json').read_text())
            masks, counts = [], []
            for index,frame in enumerate(truth['frames']):
                labels = np.load(_verified(capture,frame['objects'],truth['sha256']),allow_pickle=False)
                bits = np.load(_verified(capture,frame['visibility'],truth['sha256']),allow_pickle=False)
                expected = labels == truth['objects'][truth['diagnostic_target']['object_name']]
                masks.append((expected,bits!=0))
                counts.append({'truth_index':index,'target_pixels':int(expected.sum()),
                               'visible_target_pixels':int((expected & (bits!=0)).sum())})
            stable_truth = {k:truth[k] for k in ('objects','diagnostic_target','source_visibility','ego_object_ids',
                            'script_sha256','visibility_script_sha256','geometry_script_sha256',
                            'diagnostic_motion_script_sha256','frames')}
            stable_truth['config'] = normalized_config(truth['config'],dimensions)
            truth_controls.append({'stable':stable_truth,'counts':counts,'files_sha256':truth['sha256'],
                                   'decoded_direct_rgb_sha256':hashlib.sha256(np.asarray(direct).tobytes()).hexdigest()})
            binaries.append([json.loads((root/cid/f'native/seed{seed}'/c['id']/'invocation.json').read_text())['binary_sha256']
                             for c in low['plan']['carriers']])
            if level == 'high':
                entry = next(e for e in current['cases'] if e['id']==cid)
                native_case = entry['audit']['cases'][0]
                aliases = {'dome_floor_v1':'dome_floor','cylinder_floor_v1':'cylinder_floor','cube_floor_v1':'cube_floor'}
                reports = {}
                for carrier,digest in native_case['audit']['native_report_sha256'].items():
                    path = root/cid/f'native/seed{seed}'/aliases.get(carrier,carrier)/'report.json'
                    if sha(path) != digest:
                        raise ValueError('high native report changed after audit')
                    reports[carrier] = json.loads(path.read_text())
                from server_boundary import audited_rgba
                native_case['audit']['native_reports'] = reports
                for row in native_case['audit']['results']:
                    rgb = audited_rgba(native_case,row,root/cid/'native')[...,:3]
                    expected,visible = masks[row['truth_index']]
                    threshold = case['target']['chroma_threshold']
                    scaled = copy.deepcopy(row)
                    scaled['quality']['target'] = measure_target(target_mask(rgb,threshold),expected,32)
                    scaled['visible_target'] = measure_visible_target(target_mask(rgb,threshold),expected,visible,32)
                    scaled['object_position'] = object_position(rgb/255.,expected,threshold,32)
                    key = (cid,*(row[k] for k in ('carrier','truth_index','mode','boundary')))
                    scaled_rows[key] = normalized_metrics(scaled,360)
                del native_case['audit']['native_reports']
        if (contexts[0] != contexts[1] or metadata[0] != metadata[1] or source_digests[0] != source_digests[1]
                or truth_controls[0]['stable'] != truth_controls[1]['stable'] or binaries[0] != binaries[1]
                or cube_digests[0] != cube_digests[1] or cube_metadata[0] != cube_metadata[1]
                or file_hashes[0].keys() != file_hashes[1].keys()):
            raise ValueError('output controls differ: source/config/trajectory/truth setup/binaries')
        # Pixel centers differ, but the continuous full-box projection must scale exactly.
        high_case = next(c for c in current['asset']['cases'] if c['id']==cid)
        for pose in contexts[0][2]:
            lo,hi = box_frame_bounds(case,pose),box_frame_bounds(high_case,pose)
            if not np.allclose(np.asarray(lo['bounds_px'])*2+.5,hi['bounds_px'],rtol=0,atol=1e-9):
                raise ValueError('output analytical projection does not scale')
        controls.append({'case':cid,'decoded_fisheye_sha256':source_digests[0],'identical_source':True,
                         'decoded_cubemap_sha256':cube_digests[0],'checked_cube_faces':sum(face_counts),
                         'cube_file_hashes_differ':sum(file_hashes[0][name]!=file_hashes[1][name]
                                                     for frame in metadata[0]['frames'] for camera in frame['cameras']
                                                     for name in camera['faces'].values()),
                         'truth':{level:{k:v for k,v in value.items() if k!='stable'}
                                  for level,value in zip(('low','high'),truth_controls)}})
    a,b = sampling_rows(low),sampling_rows(current)
    if a.keys()!=b.keys() or b.keys()!=scaled_rows.keys() or len(a)!=480:
        raise ValueError('complete keyed 480-condition output comparison required')
    results = []
    for key in sorted(a):
        if a[key]['scenario_timestamp_ns'] != b[key]['scenario_timestamp_ns']:
            raise ValueError('output row timestamps differ')
        x,y,z = normalized_metrics(a[key],180),normalized_metrics(b[key],360),scaled_rows[key]
        results.append(dict(zip(('case','carrier','truth_index','mode','boundary'),key)) |
                       {'low':x,'high':y,'high_area_scaled':z,'delta':deltas(x,y),'delta_area_scaled':deltas(x,z),
                        'low_rgba_sha256':a[key]['rgba_sha256'],'high_rgba_sha256':b[key]['rgba_sha256']})
    return {'schema_version':1,'experiment':'E-STITCH-parallax-output-paired-01','plan':current['plan'],
            'reference_summary_sha256':sha(historical),'analyzer_sha256':sha(Path(__file__)),
            'parent_analyzer_sha256':sha(ROOT/'tools/research/parallax_height.py'),
            'source_fingerprint':current['source_fingerprint'],'render_device':current['render_device'],
            'native_report_sha256':{level:{c['id']:c['audit']['cases'][0]['audit']['native_report_sha256']
                                          for c in summary['cases']} for level,summary in (('low',low),('high',current))},
            'controls':controls,'results':results,
            'protocol_deviations':['raw PNG hashes differ in Blender Date/Camera/Scene/RenderTime metadata; decoded cube pixels and remaining PNG metadata must match'],
            'limitations':['fixed source pixels; output-resolution sensitivity of full pipeline, not isolated rasterizer',
                           'fixed pixel-unit algorithm parameters and RGB ROI bands change physical support',
                           '8px high primary and32px area-scaled secondary; retain undefined transitions',
                           'one flat scene, two mounts and dependent poses; no universal ranking or Aurora performance']}
