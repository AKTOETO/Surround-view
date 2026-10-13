"""Bounded photometric-evidence audit; product computation is in native renderer."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image
from source_correspondence import sha, verified


def bilinear_rgb(image, uv):
    """Independent pixel-centre encoded-RGB sampling, clamp-to-edge, in byte units."""
    image = np.asarray(image)
    uv = np.asarray(uv, float)
    if image.ndim != 3 or image.shape[2] != 3 or not image.shape[0] or not image.shape[1] or uv.shape != (2,) or not np.isfinite(uv).all():
        raise ValueError('nonempty RGB image and finite UV required')
    x, y = np.floor(uv).astype(int)
    ax, ay = uv - [x, y]
    h, w = image.shape[:2]
    return sum(image[np.clip(yy, 0, h-1), np.clip(xx, 0, w-1)].astype(float)*weight
               for xx, yy, weight in [(x,y,(1-ax)*(1-ay)), (x+1,y,ax*(1-ay)),
                                     (x,y+1,(1-ax)*ay), (x+1,y+1,ax*ay)])


def checked(root, plan_path):
    plan = json.loads(plan_path.read_text())
    for name, digest in plan['frozen_files'].items():
        verified(root/name, digest)
    return plan


def capture(root, plan_path):
    plan = checked(root, plan_path)
    output = root/plan['output_root']
    output.mkdir(exist_ok=False)
    env = dict(os.environ, **plan['backend_environment'])
    calls = []
    for case in plan['cases']:
        path = output/f"frame{case['frame']}-{case['variant']}.rgba"
        result = subprocess.run([str(root/plan['binary']), str(root/case['config']),
            str(root/plan['manifest']), str(path), 'source_disagreement_v1', str(case['frame'])],
            env=env, capture_output=True, text=True, timeout=60)
        call = dict(frame=case['frame'], variant=case['variant'], returncode=result.returncode,
                    stdout=result.stdout, stderr=result.stderr)
        if result.returncode:
            calls.append(call)
            (output/'invocations.json').write_text(json.dumps(calls, indent=2)+'\n')
            raise RuntimeError(result.stderr)
        meta = json.loads(result.stdout)
        if plan['required_device_substring'] not in meta['device']:
            raise ValueError('unexpected actual device')
        call.update(file=str(path.relative_to(root)), metadata=meta)
        calls.append(call)
    (output/'invocations.json').write_text(json.dumps(calls, indent=2)+'\n')


def collect(root, plan_path):
    plan = checked(root, plan_path)
    parent = json.loads((root/plan['truth_report']).read_text())
    prepared = json.loads((root/plan['prepared_input']).read_text())
    calls = json.loads((root/plan['output_root']/'invocations.json').read_text())
    if [(c['frame'], c['variant']) for c in calls] != [(c['frame'], c['variant']) for c in plan['cases']]:
        raise ValueError('capture selection differs')
    rows = []
    for case, call in zip(plan['cases'], calls, strict=True):
        m = call['metadata']
        if (call['returncode'] or plan['required_device_substring'] not in m['device']
                or m['config_sha256'] != sha(root/case['config'])
                or m['manifest_sha256'] != sha(root/plan['manifest'])
                or m['frame_index'] != case['frame'] or m['output_product'] != 'source_disagreement_v1'):
            raise ValueError('native capture provenance differs')
        old = json.loads((root/case['historical_report']).read_text())
        if m['inputs'] != old['inputs']:
            raise ValueError('source input identity differs')
        h, w = m['height'], m['width']
        mask = np.fromfile(verified(root/call['file'], m['payload_sha256']), np.uint8).reshape(h,w,4)
        data = prepared['frames'][case['frame']]
        images = {i['camera_id']: np.asarray(Image.open(verified(Path(i['path']), i['sha256'])).convert('RGB')) for i in m['inputs']}
        for index, coordinate in enumerate(data['coordinates_yx']):
            y,x = coordinate
            truth = next(r for r in parent['rows'] if (r['frame'],r['y'],r['x']) == (case['frame'],y,x))
            historical = next(c for c in truth['cases'] if c['variant'] == case['variant'])
            cameras = historical['eligible_cameras']
            colors = []
            for camera in cameras:
                uv = data['native_uv'][index][camera]
                if uv is None: raise ValueError('eligible source lacks retained native UV')
                colors.append(bilinear_rgb(images[camera], uv))
            expected = float(np.ptp(colors, axis=0).max()) if len(colors)>=2 else None
            p = mask[y,x].tolist()
            expected_byte = int(np.floor(expected+.5)) if expected is not None else 0
            count_equal = p[1] == len(cameras)
            encoding_equal = p[2] == (255 if p[1]>=2 else 0) and p[3] == 255 and (p[1]>=2 or p[0]==0)
            oracle_equal = count_equal and encoding_equal and abs(p[0]-expected_byte)<=plan['rgb_byte_tolerance']
            rows.append(dict(frame=case['frame'], variant=case['variant'], y=y, x=x, eligible_cameras=cameras,
                native_pixel=p, independent_rgb_samples=[c.tolist() for c in colors], independent_range_byte=expected,
                byte_error=abs(p[0]-expected_byte), count_equal=count_equal, oracle_passed=oracle_equal,
                any_matching={t: bool(historical['matched_eligible'][t]) if cameras else None for t in ['0.002','0.02']},
                any_mismatch={t: any(not truth['sources'][c]['matched'][t] for c in cameras) if cameras else None for t in ['0.002','0.02']}))
    summaries=[]
    for threshold in plan['thresholds_byte']:
        defined=[r for r in rows if r['native_pixel'][2] == 255 and r['oracle_passed']]
        summaries.append(dict(threshold_byte=threshold, defined_cells=len(defined),
            greater_than_threshold=sum(r['native_pixel'][0]>threshold for r in defined),
            any_matching_20mm=sum(r['any_matching']['0.02'] for r in defined),
            any_mismatch_20mm=sum(r['any_mismatch']['0.02'] for r in defined)))
    return dict(schema_version=1, scope=plan['scope'], plan_sha256=sha(plan_path),
        invocation_sha256=sha(root/plan['output_root']/'invocations.json'),
        all_oracles_passed=all(r['oracle_passed'] for r in rows), rows=rows, thresholds=summaries,
        limitations=plan['limitations'])


if __name__ == '__main__':
    root=Path(__file__).resolve().parents[2]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=Path('configs/research/source-disagreement-plan.json'))
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--output-report', type=Path)
    args=parser.parse_args()
    if args.capture: capture(root,args.plan.resolve())
    if args.output_report:
        args.output_report.write_text(json.dumps(collect(root,args.plan.resolve()),indent=2,allow_nan=False)+'\n')
