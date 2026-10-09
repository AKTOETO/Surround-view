"""Real production OpenCV detection/calibration on synthetic inverse-ray board images."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from calibration.raster_board import BOARD, K, SIZE, corners, detection_error, image, poses

CONDITIONS = {'clean':(0.,0.), 'blur_noise':(1.5,.02), 'heavy_blur':(4.,.02)}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def invoke(command):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=90)
    return {'returncode':result.returncode, 'stdout':result.stdout, 'stderr':result.stderr}


def study(output, binary=Path('build/sv-calibrate'), seeds=(2401,2402)):
    binary = Path(binary).resolve()
    if not binary.is_file() or not seeds or len(set(seeds)) != len(seeds):
        raise ValueError('existing calibrator and unique seeds required')
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for seed in seeds:
        geometry = poses(seed)
        for condition,(blur,noise) in CONDITIONS.items():
            folder = output/f'{seed}-{condition}'
            folder.mkdir()
            records, hashes = [], {}
            for index,(rotation,translation) in enumerate(geometry):
                name = f'board-{index:02}.png'
                pixels = image(rotation,translation,blur,noise,seed*100+index)
                Image.fromarray(pixels).save(folder/name)
                hashes[name] = digest(folder/name)
                expected = corners(rotation,translation)
                process = invoke([binary,'detect','--image',folder/name,'--output',folder/f'detect-{index:02}'])
                record = {'view':index,'split':'train' if index < 12 else 'validation',
                          'T_camera_from_board_rotation':rotation.tolist(),
                          'board_center_camera_m':translation.tolist(),
                          'exact_corners_px':expected.tolist(), 'process':process}
                if process['returncode'] == 0:
                    detected = json.loads((folder/f'detect-{index:02}'/'detections.json').read_text())
                    points = [p['uv_px'] for p in detected['points']]
                    record['detected_corners_px'] = points
                    record['localization'] = detection_error(points,expected)
                records.append(record)
            dataset = {'schema_version':1,'camera_id':0,'board':BOARD,'theta_max_rad':1.0,
                       'train':[f'board-{i:02}.png' for i in range(12)],
                       'validation':[f'board-{i:02}.png' for i in range(12,16)]}
            (folder/'dataset.json').write_text(json.dumps(dataset,indent=2)+'\n')
            entry = {'seed':seed,'condition':condition,'blur_sigma_px':blur,'noise_sigma':noise,
                     'image_sha256':hashes,'dataset_sha256':digest(folder/'dataset.json'),
                     'views':records, 'fits':[]}
            for order in (2,4):
                destination = folder/f'fit-{order}'
                fit = invoke([binary,'intrinsics','--dataset',folder/'dataset.json','--output',destination,
                              '--distortion-order',order])
                fitted = {'distortion_order':order,'process':fit}
                report = destination/'report.json'
                if report.exists():
                    fitted['report'] = json.loads(report.read_text())
                model = destination/'intrinsics.json'
                if model.exists():
                    estimate = json.loads(model.read_text())
                    fitted['estimated_intrinsics'] = estimate
                    fitted['focal_relative_error'] = [estimate['fx']/K[0,0]-1,estimate['fy']/K[1,1]-1]
                entry['fits'].append(fitted)
            results.append(entry)
            print(f'{seed} {condition}: detected {sum("localization" in r for r in records)}/16; '
                  f'fit exits {[f["process"]["returncode"] for f in entry["fits"]]}',flush=True)
    summary = {'schema_version':1,'experiment':'E-CAL-raster-01','data_kind':'synthetic inverse-ray raster',
               'production_binary_sha256':digest(binary),'image_size':SIZE,'true_K':K.tolist(),
               'true_distortion':[0.,0.,0.,0.], 'seed_units':list(seeds),
               'code_sha256':{p.name:digest(p) for p in (Path(__file__),Path(__file__).with_name('raster_board.py'),
                   Path(__file__).resolve().parents[2]/'src/vision/calibration.cpp',
                   Path(__file__).resolve().parents[2]/'src/apps/calibrate.cpp',
                   Path(__file__).resolve().parents[2]/'include/sv/vision.hpp')},
               'limitations':['one equidistant optical family, no physical photos',
                              'validation board pose fitted separately; not vehicle mount error',
                              'default CLI p95<=1px gate is technical, not physically validated',
                              'exploratory seeds; not a confirmatory holdout',
                              'global Gaussian blur/noise only; no glare or rolling shutter'],
               'results':results}
    (output/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--binary',type=Path,default=Path('build/sv-calibrate'))
    parser.add_argument('--seeds',type=int,nargs='+',default=[2401,2402])
    args = parser.parse_args()
    study(args.output,args.binary,args.seeds)
