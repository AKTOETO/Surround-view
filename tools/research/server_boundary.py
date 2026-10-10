"""Independent quality audit of RGBA captured by native svctl research.

Consumes reports only; never renders images or controls a product process.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from object_metrics import measure_target, remove_small, target_mask
from temporal_seam_stability import _verified, load_sequence


def linear(rgb):
    rgb = np.asarray(rgb, float)
    return np.where(rgb <= .04045, rgb/12.92, ((rgb+.055)/1.055)**2.4)


def quality(rgb, truth, roi, target, threshold, minimum):
    rgb, truth, roi, target = (np.asarray(x) for x in (rgb, truth, roi, target))
    if (rgb.shape != truth.shape or rgb.shape != (*roi.shape, 3) or roi.shape != target.shape
            or not roi.any() or not np.isfinite(rgb).all() or not np.isfinite(truth).all()
            or rgb.min() < 0 or rgb.max() > 1 or truth.min() < 0 or truth.max() > 1):
        raise ValueError('finite matching RGB [0,1], target and nonempty ROI required')
    error = np.abs(linear(rgb)-linear(truth))[roi]
    observed = target_mask(rgb, threshold)
    clean, _ = remove_small(observed, minimum)
    expected, _ = remove_small(target, minimum)
    result = measure_target(observed, target, minimum)
    result['false_positive_pixels'] = int((clean & ~expected).sum())
    result['missing_pixels'] = int((expected & ~clean).sum())
    return {'roi_pixels': int(roi.sum()), 'linear_mae': float(error.mean()),
            'linear_channel_error_p95': float(np.percentile(error, 95)),
            'srgb_mae': float(np.abs(rgb-truth)[roi].mean()), 'target': result}


def timestamp(metadata):
    inputs = metadata['inputs']
    if len(inputs) != 4 or [x['camera_id'] for x in inputs] != list(range(4)) or not all(x['used'] for x in inputs):
        raise ValueError('four ordered used inputs required')
    stamps = {x['source_timestamp_ns'] for x in inputs}
    if len(stamps) != 1 or any(x['source_clock_domain'] != 'scenario' for x in inputs):
        raise ValueError('synchronized scenario timestamps required')
    return stamps.pop()


def audit(fixture, capture, report_paths):
    fixture, capture = Path(fixture).resolve(), Path(capture).resolve()
    cfg, _, truths, rois, stamps, _ = load_sequence(fixture, capture, 'any')
    paired = json.loads((capture/'paired_truth.json').read_text())
    target = paired['diagnostic_target']
    target_id = paired['objects'][target['object_name']]
    targets = [np.load(_verified(capture, frame['objects'], paired['sha256']), allow_pickle=False) == target_id
               for frame in paired['frames']]
    by_stamp = {str(stamp): index for index, stamp in enumerate(stamps)}
    rows, native_reports, hashes, config_hashes = [], {}, {}, {}
    for path in map(Path, report_paths):
        native = json.loads(path.read_text())
        if not native['success'] or not native['restored'] or native['cursor_restored']:
            raise ValueError('successful settings restore and explicit non-restored cursor required')
        if native['last_baseline_rgba_sha256'] != native['restored_rgba_sha256']:
            raise ValueError('last-frame baseline must match restored output')
        carrier = native['initial_state']['surface']['type']
        if carrier == 'rectangular_bowl_v1':
            carrier = 'plane' if native['initial_state']['surface']['corner_height_m'] == 0 else 'bowl'
        if carrier in native_reports:
            raise ValueError('one report per carrier required')
        effective = json.loads((path.parent/'config.json').read_text())
        expected_config = dict(cfg, surface=native['initial_state']['surface'])
        if effective != expected_config:
            raise ValueError('native configuration differs outside carrier')
        for name in ('azimuth_rad', 'elevation_rad', 'distance_m'):
            if native['initial_state'][name] != cfg['virtual_camera'][name]:
                raise ValueError('virtual view differs from direct truth')
        baselines = native['frame_baselines']
        if len(baselines) != len(stamps) or {timestamp(b['metadata']) for b in baselines} != set(by_stamp):
            raise ValueError('each direct-truth frame must occur once')
        variants = native['scenario']['variants']
        profiles = {(v['mode'],v['pyramid_boundary']) for v in variants}
        if len(variants) != 4 or profiles != {(m,b) for m in ('multi_band','graph_cut_multi_band') for b in ('zero','normalized')}:
            raise ValueError('complete four-profile boundary matrix required')
        if len(native['samples']) != len(stamps)*4*(native['scenario']['warmup']+native['scenario']['repeats']):
            raise ValueError('incomplete trial matrix')
        for frame_index, baseline in enumerate(baselines):
            stamp = timestamp(baseline['metadata'])
            truth_index = by_stamp[stamp]
            for variant, settings in enumerate(variants):
                samples = [s for s in native['samples'] if s['frame_index'] == frame_index and s['variant'] == variant]
                if {s['block'] for s in samples} != set(range(native['scenario']['warmup']+native['scenario']['repeats'])):
                    raise ValueError('missing randomized block')
                digests = set()
                measured = []
                rgb = None
                for sample in samples:
                    meta = sample['metadata']
                    if (meta['health'] != 'READY' or meta['inputs'] != baseline['metadata']['inputs']
                            or meta['frame_set_id'] != baseline['metadata']['frame_set_id']
                            or meta['surface'] != native['initial_state']['surface']
                            or [x['calibration_id'] for x in meta['inputs']] != [c['calibration_id'] for c in cfg['cameras']]
                            or meta['fusion'] != {k:v for k,v in settings.items() if k != 'surface'}):
                        raise ValueError('trial input/fusion provenance mismatch')
                    if (meta['width'] != cfg['output']['width'] or meta['height'] != cfg['output']['height']
                            or meta['stride_bytes'] != 4*meta['width']
                            or meta['pixel_format'] != 'RGBA8' or meta['row_origin'] != 'top_left'):
                        raise ValueError('unexpected capture layout')
                    digests.add(sample['rgba_sha256'])
                    if sample['warmup']:
                        continue
                    filename = (path.parent/sample['rgba_file']).resolve()
                    filename.relative_to(path.parent.resolve())
                    payload = filename.read_bytes()
                    if hashlib.sha256(payload).hexdigest() != sample['rgba_sha256']:
                        raise ValueError('captured RGBA hash mismatch')
                    h, w = cfg['output']['height'], cfg['output']['width']
                    if meta['height'] != h or meta['width'] != w or len(payload) != h*w*4:
                        raise ValueError('native/direct output size mismatch')
                    rgba = np.frombuffer(payload, np.uint8).reshape(h,w,4)
                    if not (rgba[...,3] == 255).all():
                        raise ValueError('opaque final output required')
                    rgb = rgba[...,:3].astype(float)/255
                    measured.append(meta['pipeline_spans_ms'])
                if len(digests) != 1 or len(measured) != native['scenario']['repeats']:
                    raise ValueError('nonrepeatable output or missing measurements')
                rows.append({'carrier':carrier, 'frame_index':frame_index, 'truth_index':truth_index,
                             'scenario_timestamp_ns':stamp, 'mode':settings['mode'],
                             'boundary':settings['pyramid_boundary'], 'rgba_sha256':next(iter(digests)),
                             'quality':quality(rgb,truths[truth_index],rois[truth_index],targets[truth_index],
                                               target['chroma_threshold'],target['min_component_pixels']),
                             'stage_samples_ms':measured})
        native_reports[carrier] = native
        hashes[carrier] = hashlib.sha256(path.read_bytes()).hexdigest()
        config_hashes[carrier] = hashlib.sha256((path.parent/'config.json').read_bytes()).hexdigest()
    pairs = []
    for zero in (r for r in rows if r['boundary'] == 'zero'):
        normalized = next(r for r in rows if r['carrier'] == zero['carrier'] and
                          r['frame_index'] == zero['frame_index'] and r['mode'] == zero['mode'] and r['boundary'] == 'normalized')
        pairs.append({'carrier':zero['carrier'], 'truth_index':zero['truth_index'], 'mode':zero['mode'],
                      'linear_mae_delta':normalized['quality']['linear_mae']-zero['quality']['linear_mae'],
                      'target_iou_delta':normalized['quality']['target']['iou']-zero['quality']['target']['iou'],
                      'fusion_cpu_p50_delta_ms':float(np.median([s['fusion_cpu'] for s in normalized['stage_samples_ms']])-
                                                    np.median([s['fusion_cpu'] for s in zero['stage_samples_ms']]))})
    protocol = Path(__file__).resolve().parents[2]/'docs/research/SERVER_BOUNDARY_PROTOCOL.md'
    return {'schema_version':1, 'experiment':'E-STITCH-server-boundary-01',
            'protocol_sha256':hashlib.sha256(protocol.read_bytes()).hexdigest(),
            'fixture_sha256':{name:hashlib.sha256((fixture/name).read_bytes()).hexdigest()
                              for name in ('config.json','manifest.json','ground_truth.json')},
            'capture_sha256':{name:hashlib.sha256((capture/name).read_bytes()).hexdigest()
                              for name in ('capture.json','paired_truth.json')},
            'analyzer_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'metric_code_sha256':{name:hashlib.sha256((Path(__file__).resolve().parents[1]/name).read_bytes()).hexdigest()
                                  for name in ('object_metrics.py','temporal_seam_stability.py')},
            'numpy_version':np.__version__,
            'native_report_sha256':hashes, 'native_config_sha256':config_hashes,
            'results':rows, 'paired_differences':pairs,
            'native_reports':native_reports,
            'limitations':['exploratory reused fixture; not holdout', 'coded target is not natural-object ghost truth',
                           'carriers have unequal mesh budgets; compare boundaries within carrier',
                           'paused sequence has no history/cursor reset and is not sustained FPS']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--report',type=Path,action='append',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    result = audit(args.fixture,args.capture or args.fixture,args.report)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({'conditions':len(result['results']), 'pairs':len(result['paired_differences'])}))
