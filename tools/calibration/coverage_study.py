"""Paired central/peripheral training at fixed view budget and common validation."""
import argparse
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from calibration.coverage import coverage, peripheral_poses
from calibration.optical_models import FAMILIES, metric_validation
from calibration.raster_board import BOARD, K, corners, detection_error, image, poses
from calibration.raster_study import digest, invoke


def fit(binary, folder, dataset, output, order):
    process = invoke([binary,'intrinsics','--dataset',folder/dataset,'--output',output,
                      '--distortion-order',order])
    result = {'process':process}
    for name,key in [('report.json','report'),('intrinsics.json','estimate')]:
        if (output/name).exists():
            result[key] = json.loads((output/name).read_text())
    return result


def study(output,binary=Path('build/sv-calibrate'),seeds=(4101,4102)):
    output,binary = Path(output).resolve(),Path(binary).resolve()
    if not binary.is_file() or not seeds or len(set(seeds)) != len(seeds):
        raise ValueError('existing calibrator and unique seeds required')
    output.mkdir(parents=True,exist_ok=False)
    results = []
    for family in FAMILIES:
        for seed in seeds:
            folder = output/f'{family.name}-{seed}'
            folder.mkdir()
            central = poses(seed)
            groups = {'central_train':central[:12], 'outer_train':peripheral_poses(seed+1000),
                      'central_validation':central[12:],'outer_validation':peripheral_poses(seed+2000)}
            files,records,hashes = {},[],{}
            for group,geometry in groups.items():
                files[group] = []
                for i,(rotation,translation) in enumerate(geometry):
                    name = f'{group}-{i:02}.png'
                    files[group].append(name)
                    Image.fromarray(image(rotation,translation,optics=family)).save(folder/name)
                    hashes[name] = digest(folder/name)
                    process = invoke([binary,'detect','--image',folder/name,'--output',folder/f'detect-{group}-{i}'])
                    row = {'group':group,'view':i,'file':name,'process':process}
                    if process['returncode'] == 0:
                        points = json.loads((folder/f'detect-{group}-{i}'/'detections.json').read_text())['points']
                        row['localization'] = detection_error([p['uv_px'] for p in points],corners(rotation,translation,family))
                    records.append(row)
            entry = {'family':family.name,'seed':seed,'true_coefficients':family.coefficients,
                     'image_sha256':hashes,'views':records,'profiles':[]}
            for profile in ('central','wide'):
                train = files['central_train'] if profile=='central' else files['central_train'][:4]+files['outer_train']
                geometry = groups['central_train'] if profile=='central' else groups['central_train'][:4]+groups['outer_train']
                datasets,hashes_d = {},{}
                for gate in ('central','full'):
                    validation = files['central_validation']+(files['outer_validation'] if gate=='full' else [])
                    name = f'{profile}-{gate}.json'
                    data = {'schema_version':1,'camera_id':0,'board':BOARD,'theta_max_rad':1.,
                            'train':train,'validation':validation}
                    (folder/name).write_text(json.dumps(data,indent=2)+'\n')
                    datasets[gate],hashes_d[gate] = name,digest(folder/name)
                result = {'profile':profile,'train_coverage':coverage(geometry,family),
                          'dataset_sha256':hashes_d,'fits':[]}
                for order in (2,4):
                    central_fit = fit(binary,folder,datasets['central'],folder/f'{profile}-central-fit-{order}',order)
                    full_fit = fit(binary,folder,datasets['full'],folder/f'{profile}-full-fit-{order}',order)
                    fitted = {'order':order,'central_gate':central_fit,'full_gate':full_fit}
                    if 'estimate' in central_fit:
                        fitted['metric_validation'] = metric_validation(central_fit['estimate'],K,family,groups['central_validation'])
                    if 'estimate' in central_fit and 'estimate' in full_fit:
                        a,b = central_fit['estimate'],full_fit['estimate']
                        av,bv = ([m['fx'],m['fy'],m['cx'],m['cy'],*m['k']] for m in (a,b))
                        fitted['repeat_fit_max_parameter_delta'] = float(np.max(np.abs(np.asarray(av)-bv)))
                    result['fits'].append(fitted)
                entry['profiles'].append(result)
            entry['validation_coverage'] = {name:coverage(groups[name],family) for name in ('central_validation','outer_validation')}
            entry['geometry'] = {group:[{'rotation':r.tolist(),'translation':t.tolist()} for r,t in geometry] for group,geometry in groups.items()}
            results.append(entry)
            print(f'{family.name} {seed}: detected {sum("localization" in r for r in records)}/32; '
                  f'central/full exits {[[(f["central_gate"]["process"]["returncode"],f["full_gate"]["process"]["returncode"]) for f in p["fits"]] for p in entry["profiles"]]}',flush=True)
    root = Path(__file__).resolve().parents[2]
    sources = [Path(__file__),Path(__file__).with_name('coverage.py'),Path(__file__).with_name('optical_models.py'),
               Path(__file__).with_name('raster_board.py'),Path(__file__).with_name('raster_study.py'),
               root/'src/vision/calibration.cpp',root/'src/apps/calibrate.cpp']
    report = {'schema_version':1,'experiment':'E-CAL-coverage-01','seeds':list(seeds),'true_K':K.tolist(),
              'production_binary_sha256':digest(binary),'code_sha256':{str(p.relative_to(root)):digest(p) for p in sources},
              'limitations':['synthetic radial optics, clean images, no real cameras',
                             'equal 12-view training budget; wide replaces eight central views',
                             'central and full gate runs refit the same training data separately',
                             'metric checks use central-gate exported models and common true geometry',
                             'exploratory pose design; not preregistered confirmatory evidence'],
              'results':results}
    (output/'summary.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--binary',type=Path,default=Path('build/sv-calibrate'))
    parser.add_argument('--seeds',type=int,nargs='+',default=[4101,4102])
    args = parser.parse_args()
    study(args.output,args.binary,args.seeds)
