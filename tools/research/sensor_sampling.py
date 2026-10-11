"""Fisheye raster sensitivity over native server reports and independent fixed truth."""
import copy
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

from blender.camera_sampling import resample_config
from parallax_output import canonical_truth
from temporal_seam_stability import _verified, load_sequence

ROOT = Path(__file__).resolve().parents[2]


def validate_sensor_design(low_plan, high_plan, low_asset, high_asset):
    from parallax_height import validate_factors
    plans, assets = copy.deepcopy([low_plan,high_plan]),copy.deepcopy([low_asset,high_asset])
    for plan in plans:
        if plan['output'] != [640,360]:
            raise ValueError('frozen640x360 output required')
        for key in ('experiment','inputs_plan','protocol','reference'):
            plan.pop(key)
    for case in assets[1]['cases']:
        if case.pop('fisheye_resolution') != [800,800]:
            raise ValueError('frozen800x800 high fisheye grid required')
    for asset in assets:
        validate_factors(asset)
        if any(c['capture']['face_size'] != 512 or (c['capture']['width'],c['capture']['height']) != (640,360)
               for c in asset['cases']):
            raise ValueError('fixed cubemap512/output640x360 required')
    if plans[0] != plans[1] or assets[0] != assets[1]:
        raise ValueError('sensor experiment changed a non-input factor')


def cube_control(capture, info):
    digest = hashlib.sha256()
    profiles, names = [], []
    for frame in info['frames']:
        for camera in frame['cameras']:
            for face,name in sorted(camera['faces'].items()):
                with Image.open(_verified(capture,name,info['sha256'])) as image:
                    if image.size != (512,512) or image.mode != 'RGB':
                        raise ValueError('fixed source cube512/RGB required')
                    digest.update(f'{len(names)}:{camera["id"]}:{face}'.encode())
                    digest.update(np.asarray(image).tobytes())
                    profiles.append({k:v for k,v in image.info.items() if k not in ('Date','Camera','Scene','RenderTime')})
                    names.append(name)
    return digest.hexdigest(),profiles,names


def sensor_followup(current, high_root, high_inputs):
    from parallax_height import audit, sha, verify_historical_results, pair_sampling_rows
    reference = current['plan']['reference']
    low_root,low_inputs = ROOT/reference['root'],ROOT/reference['inputs']
    historical = ROOT/reference['summary']
    if sha(historical) != reference['summary_sha256']:
        raise ValueError('frozen sensor reference summary checksum mismatch')
    low = audit(ROOT/reference['plan'],low_root,low_inputs)
    verify_historical_results(low,json.loads(historical.read_text()))
    validate_sensor_design(low['plan'],current['plan'],low['asset'],current['asset'])
    if low['source_fingerprint'] != current['source_fingerprint'] or low['render_device'] != current['render_device']:
        raise ValueError('sensor experiment implementation/device changed')
    provenance = json.loads((high_inputs/'provenance.json').read_text())
    for helper in ('camera_sampling.py','parallax_sensor.py'):
        if provenance['generator_sha256'].get(helper) != sha(ROOT/'tools/blender'/helper):
            raise ValueError('sensor generator missing or changed')
    controls = []
    for case in low['asset']['cases']:
        cid,seed = case['id'],case['scenario']['seed']
        configs,metadata,truths,frames,cubes,binaries,optics,original_tables = [],[],[],[],[],[],[],[]
        input_counts = []
        for root,inputs,size in ((low_root,low_inputs,400),(high_root,high_inputs,800)):
            capture = inputs/cid/f'seed{seed}-capture'
            cfg,images,direct,_,stamps,poses = load_sequence(inputs/cid/f'seed{seed}-inputs',capture,'any')
            if any(image.shape != (size,size,3) for frame in images for image in frame):
                raise ValueError('actual fisheye dimensions differ')
            configs.append(cfg);frames.append((stamps,poses))
            info = json.loads((capture/'capture.json').read_text())
            error = info['optics_check']
            if error['max_error_px'] > error['threshold_px'] or error['threshold_px'] != .001:
                raise ValueError('actual optics check outside frozen tolerance')
            optics.append(error)
            record = copy.deepcopy(info)
            for key in ('sha256','optics_check'):
                record.pop(key)
            metadata.append(record)
            cubes.append(cube_control(capture,info))
            truth = json.loads((capture/'paired_truth.json').read_text())
            original_tables.append(truth['objects'])
            digest = hashlib.sha256(np.asarray(direct).tobytes())
            for frame in truth['frames']:
                for key in ('objects','visibility'):
                    value = np.load(_verified(capture,frame[key],truth['sha256']),allow_pickle=False)
                    digest.update(str((value.shape,value.dtype.str)).encode());digest.update(value.tobytes())
            truths.append((canonical_truth(truth),digest.hexdigest(),truth['config']))
            binaries.append([json.loads((root/cid/f'native/seed{seed}'/c['id']/'invocation.json').read_text())['binary_sha256']
                             for c in low['plan']['carriers']])
            input_counts.append([int(sum(np.count_nonzero(np.minimum(image[...,0],image[...,2]).astype(float)-image[...,1]
                                                           >255*case['target']['chroma_threshold']) for image in frame))
                                 for frame in images])
        expected = copy.deepcopy(metadata[0])
        for key in ('config','nominal_config'):
            expected[key] = resample_config(expected[key])
        if (configs[1] != resample_config(configs[0]) or metadata[1] != expected or frames[0] != frames[1]
                or truths[0][:2] != truths[1][:2] or truths[1][2] != resample_config(truths[0][2])
                or cubes[0] != cubes[1] or binaries[0] != binaries[1]):
            raise ValueError('sensor controls differ: intrinsics-only scale/cubes/truth/trajectory/binaries')
        controls.append({'case':cid,'identical_cube_rgb_sha256':cubes[0][0],
                         'checked_cube_faces':len(cubes[0][2])+len(cubes[1][2]),
                         'identical_decoded_truth_sha256':truths[0][1],'intrinsics_only_scale':True,
                         'optics_checks':optics,'object_tables':original_tables,
                         'input_chroma_pixels':dict(zip(('low','high'),input_counts))})
    results = pair_sampling_rows(low,current)
    if len(results) != 480:
        raise ValueError('complete480-condition sensor comparison required')
    return {'schema_version':1,'experiment':'E-STITCH-parallax-sensor-paired-01','plan':current['plan'],
            'reference_summary_sha256':sha(historical),'analyzer_sha256':sha(Path(__file__)),
            'parent_analyzer_sha256':sha(ROOT/'tools/research/parallax_height.py'),
            'source_fingerprint':current['source_fingerprint'],'render_device':current['render_device'],
            'native_report_sha256':{level:{c['id']:c['audit']['cases'][0]['audit']['native_report_sha256']
                                          for c in summary['cases']} for level,summary in (('low',low),('high',current))},
            'controls':controls,'results':results,
            'limitations':['synthetic fisheye resampling of fixed512px cubemap, not a new sensor or independent scene',
                           'unchanged pixel-unit source edge settings; full-pipeline input-resolution sensitivity',
                           'one flat world/two mounts/dependent poses; no universal carrier ranking or Aurora timing']}
