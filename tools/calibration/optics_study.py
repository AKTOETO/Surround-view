"""Multi-family image calibration with fixed-pose and known-plane metric validation."""
import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from calibration.optical_models import FAMILIES, metric_validation
from calibration.raster_board import BOARD, K, SIZE, corners, detection_error, image, poses
from calibration.raster_study import digest, invoke


def study(output, binary=Path('build/sv-calibrate'), seeds=(3101,3102)):
    output,binary = Path(output).resolve(),Path(binary).resolve()
    if not binary.is_file() or not seeds or len(set(seeds)) != len(seeds):
        raise ValueError('existing calibrator and distinct seeds required')
    output.mkdir(parents=True,exist_ok=False)
    results = []
    for family in FAMILIES:
        for seed in seeds:
            geometry = poses(seed)
            for condition,blur,noise in [('clean',0.,0.),('blur_noise',1.5,.02)]:
                folder = output/f'{family.name}-{seed}-{condition}'
                folder.mkdir()
                records,hashes = [],{}
                for index,(rotation,translation) in enumerate(geometry):
                    name = f'board-{index:02}.png'
                    Image.fromarray(image(rotation,translation,blur,noise,seed*100+index,optics=family)).save(folder/name)
                    hashes[name] = digest(folder/name)
                    process = invoke([binary,'detect','--image',folder/name,'--output',folder/f'detect-{index:02}'])
                    row = {'view':index,'process':process}
                    if process['returncode'] == 0:
                        points = json.loads((folder/f'detect-{index:02}'/'detections.json').read_text())['points']
                        row['localization'] = detection_error([p['uv_px'] for p in points],corners(rotation,translation,family))
                    records.append(row)
                dataset = {'schema_version':1,'camera_id':0,'board':BOARD,'theta_max_rad':1.,
                           'train':[f'board-{i:02}.png' for i in range(12)],
                           'validation':[f'board-{i:02}.png' for i in range(12,16)]}
                (folder/'dataset.json').write_text(json.dumps(dataset,indent=2)+'\n')
                entry = {'family':family.name,'true_coefficients':family.coefficients,'seed':seed,
                         'condition':condition,'blur_sigma_px':blur,'noise_sigma':noise,
                         'image_sha256':hashes,'dataset_sha256':digest(folder/'dataset.json'),
                         'geometry':[{'rotation':r.tolist(),'translation':t.tolist()} for r,t in geometry],
                         'views':records,'fits':[]}
                for order in (2,4):
                    destination = folder/f'fit-{order}'
                    process = invoke([binary,'intrinsics','--dataset',folder/'dataset.json','--output',destination,
                                      '--distortion-order',order])
                    fitted = {'order':order,'process':process}
                    if (destination/'report.json').exists():
                        fitted['report'] = json.loads((destination/'report.json').read_text())
                    if (destination/'intrinsics.json').exists():
                        estimate = json.loads((destination/'intrinsics.json').read_text())
                        fitted['estimate'] = estimate
                        fitted['metric_validation'] = metric_validation(estimate,K,family,geometry[12:],SIZE)
                    entry['fits'].append(fitted)
                results.append(entry)
                print(f'{family.name} {seed} {condition}: detected {sum("localization" in r for r in records)}/16, '
                      f'fit exits {[f["process"]["returncode"] for f in entry["fits"]]}',flush=True)
    root = Path(__file__).resolve().parents[2]
    sources = [Path(__file__),root/'tools/calibration/optical_models.py',root/'tools/calibration/raster_board.py',
               root/'tools/calibration/raster_study.py',root/'src/vision/calibration.cpp',root/'src/apps/calibrate.cpp']
    report = {'schema_version':1,'experiment':'E-CAL-optics-01','seeds':list(seeds),'true_K':K.tolist(),
              'production_binary_sha256':digest(binary), 'code_sha256':{str(p.relative_to(root)):digest(p) for p in sources},
              'limitations':['synthetic optical models, no physical camera validation',
                             'exploratory central board poses, no full-field training',
                             'metric validation uses exactly known synthetic board/camera/floor geometry',
                             'only accepted models exported by production CLI can be evaluated',
                             'all fits are KB polynomials; this does not compare different production solver families'],
              'results':results}
    (output/'summary.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--binary',type=Path,default=Path('build/sv-calibrate'))
    parser.add_argument('--seeds',type=int,nargs='+',default=[3101,3102])
    args = parser.parse_args()
    study(args.output,args.binary,args.seeds)
