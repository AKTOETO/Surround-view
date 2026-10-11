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


def audit(fixture, capture, report_paths, comparison='pyramid_boundary'):
    if comparison not in ('pyramid_boundary', 'seam_solver'):
        raise ValueError('unsupported comparison')
    seam = comparison == 'seam_solver'
    modes = ('graph_cut_seam', 'graph_cut_multi_band') if seam else ('multi_band', 'graph_cut_multi_band')
    policies = ('binary_pairs', 'alpha_expansion') if seam else ('zero', 'normalized')
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
        profiles = {(v['mode'],v.get(comparison,policies[0])) for v in variants}
        if len(variants) != 4 or profiles != {(m,b) for m in modes for b in policies}:
            raise ValueError('complete four-profile comparison matrix required')
        if seam and any(v['pyramid_boundary'] != 'zero' for v in variants):
            raise ValueError('seam comparison requires fixed zero boundary')
        if not seam and any(v.get('seam_solver','binary_pairs') != 'binary_pairs' for v in variants):
            raise ValueError('boundary comparison requires legacy seam solver')
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
                optimization = None
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
                    if settings.get('seam_solver','binary_pairs') == 'alpha_expansion':
                        stats = meta['seam_optimization']
                        if (stats['implementation'] != 'integer_potts_v1' or stats['max_sweeps'] != 8
                                or not 1 <= stats['sweeps'] <= 8
                                or not 0 <= stats['final_energy'] <= stats['initial_energy']
                                or stats['nodes'] <= 0 or stats['edges'] < 0):
                            raise ValueError('invalid seam optimization telemetry')
                        if optimization is not None and stats != optimization:
                            raise ValueError('nonrepeatable seam optimization')
                        optimization = stats
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
                             'seam_solver':settings.get('seam_solver','binary_pairs'),
                             'seam_optimization':optimization,
                             'quality':quality(rgb,truths[truth_index],rois[truth_index],targets[truth_index],
                                               target['chroma_threshold'],target['min_component_pixels']),
                             'stage_samples_ms':measured})
        native_reports[carrier] = native
        hashes[carrier] = hashlib.sha256(path.read_bytes()).hexdigest()
        config_hashes[carrier] = hashlib.sha256((path.parent/'config.json').read_bytes()).hexdigest()
    pairs = []
    axis = 'seam_solver' if seam else 'boundary'
    for zero in (r for r in rows if r[axis] == policies[0]):
        normalized = next(r for r in rows if r['carrier'] == zero['carrier'] and
                          r['frame_index'] == zero['frame_index'] and r['mode'] == zero['mode'] and r[axis] == policies[1])
        pairs.append({'carrier':zero['carrier'], 'truth_index':zero['truth_index'], 'mode':zero['mode'],
                      'linear_mae_delta':normalized['quality']['linear_mae']-zero['quality']['linear_mae'],
                      'target_iou_delta':normalized['quality']['target']['iou']-zero['quality']['target']['iou'],
                      'fusion_cpu_p50_delta_ms':float(np.median([s['fusion_cpu'] for s in normalized['stage_samples_ms']])-
                                                    np.median([s['fusion_cpu'] for s in zero['stage_samples_ms']]))})
    protocol = Path(__file__).resolve().parents[2]/'docs/research'/('MULTILABEL_SEAM_PROTOCOL.md' if seam else 'SERVER_BOUNDARY_PROTOCOL.md')
    return {'schema_version':1, 'experiment':'E-STITCH-server-seam-01' if seam else 'E-STITCH-server-boundary-01',
            'comparison':comparison, 'difference':policies[1]+' minus '+policies[0],
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


def verify_capture_case(case, root):
    from blender.scenario import validate
    seed = case['scenario']['seed']
    capture = root/f'seed{seed}-capture'
    info = json.loads((capture/'capture.json').read_text())
    truth = json.loads((capture/'paired_truth.json').read_text())
    scripts = dict(info['script_sha256'])
    scripts.update({'paired_truth.py':truth['script_sha256'],
                    'visibility.py':truth['visibility_script_sha256'],
                    'geometry_truth.py':truth['geometry_script_sha256'],
                    'diagnostic_motion.py':truth['diagnostic_motion_script_sha256']})
    for name,digest in scripts.items():
        if hashlib.sha256((Path(__file__).resolve().parents[1]/'blender'/name).read_bytes()).hexdigest() != digest:
            raise ValueError('recorded capture helper does not match source')
    if info['scenario_recipe'] != validate(case['scenario']):
        raise ValueError('scene recipe differs from frozen plan')
    if {k:truth['diagnostic_target'][k] for k in case['target']} != case['target']:
        raise ValueError('diagnostic target differs from frozen plan')
    if (info['face_size'] != case['capture']['face_size']
            or len(info['frames']) != case['capture']['frames']
            or info['config']['output'] != {'width':case['capture']['width'],'height':case['capture']['height']}):
        raise ValueError('capture dimensions differ from plan')
    expected_stamps = [str(round(n*case['capture']['frame_step']*1e9/info['fps']))
                       for n in range(case['capture']['frames'])]
    if [frame['scenario_timestamp_ns'] for frame in info['frames']] != expected_stamps:
        raise ValueError('capture frame spacing differs from plan')


def audit_study(plan_path, root):
    """Verify all predeclared procedural cases; no process launch or rendering."""
    plan_path, root = Path(plan_path), Path(root)
    plan = json.loads(plan_path.read_text())
    if plan.get('schema_version') != 1 or len(plan.get('cases',[])) != 3:
        raise ValueError('complete three-case frozen study required')
    provenance = json.loads((root/'provenance.json').read_text())
    if provenance['plan_sha256'] != hashlib.sha256(plan_path.read_bytes()).hexdigest():
        raise ValueError('capture plan hash mismatch')
    for name, digest in provenance['generator_sha256'].items():
        if hashlib.sha256((Path(__file__).resolve().parents[1]/'blender'/name).read_bytes()).hexdigest() != digest:
            raise ValueError('capture generator changed after freeze')
    cases = []
    seeds = set()
    for case in plan['cases']:
        seed = case['scenario']['seed']
        if seed in seeds:
            raise ValueError('unique study seeds required')
        seeds.add(seed)
        verify_capture_case(case, root)
        capture = root/f'seed{seed}-capture'
        result = audit(root/f'seed{seed}-inputs',capture,
                       [root/f'seed{seed}-run/dome_floor/report.json'],'seam_solver')
        cases.append({'id':case['id'],'seed':seed,'audit':result})
    protocol = Path(__file__).resolve().parents[2]/'docs/research/SEAM_GENERALIZATION_PROTOCOL.md'
    return {'schema_version':1,'experiment':'E-STITCH-seam-generalization-01',
            'comparison':'seam_solver','difference':'alpha_expansion minus binary_pairs',
            'protocol_sha256':hashlib.sha256(protocol.read_bytes()).hexdigest(),
            'plan':plan,'provenance':provenance,'cases':cases,
            'limitations':['three new instances of one procedural street family; no real images',
                           'exact perturbed calibration used; not calibration solver validation',
                           'two frames per instance; no sustained latency or temporal-history test']}


def check_mesh_budget(metadata, plan):
    resources = metadata['mesh_resources']
    active, resident = resources['active'], resources['resident']
    if resources['scope'] != 'carrier_position_index_buffers' or active != resident:
        raise ValueError('fresh carrier buffers required; inactive resident allocations present')
    if (any(type(v) is not int or v < 0 for v in active.values())
            or active['triangles'] != metadata['mesh_triangles']
            or active['indices'] != 3*active['triangles']
            or active['vertex_buffer_bytes'] != 12*active['vertices']
            or active['index_buffer_bytes'] != 4*active['indices']
            or active['buffer_bytes'] != active['vertex_buffer_bytes']+active['index_buffer_bytes']
            or not plan['triangles_min'] <= active['triangles'] <= plan['triangles_max']
            or active['vertices'] > plan['vertices_max']
            or active['buffer_bytes'] > plan['buffer_bytes_max']):
        raise ValueError('carrier mesh resource accounting or budget violation')
    return active


def audit_budget(plan_path, root, inputs_root, protocol_name='CARRIER_BUDGET_PROTOCOL.md'):
    plan_path, root, inputs_root = Path(plan_path), Path(root), Path(inputs_root)
    repository = Path(__file__).resolve().parents[2]
    plan = json.loads(plan_path.read_text())
    provenance = json.loads((root/'provenance.json').read_text())
    if provenance['plan_sha256'] != hashlib.sha256(plan_path.read_bytes()).hexdigest():
        raise ValueError('budget plan changed after freeze')
    input_path = repository/plan['inputs_plan']
    scenario_path = repository/plan['scenario']
    if (provenance['inputs_plan_sha256'] != hashlib.sha256(input_path.read_bytes()).hexdigest()
            or provenance['scenario_sha256'] != hashlib.sha256(scenario_path.read_bytes()).hexdigest()):
        raise ValueError('budget input plan/scenario changed after freeze')
    cases = json.loads(input_path.read_text())['cases']
    input_provenance = json.loads((inputs_root/'provenance.json').read_text())
    if input_provenance['plan_sha256'] != provenance['inputs_plan_sha256']:
        raise ValueError('capture input plan differs')
    scenario = json.loads(scenario_path.read_text())
    output, fingerprints = [], set()
    for seed in plan['seeds']:
        case = next(c for c in cases if c['scenario']['seed'] == seed)
        verify_capture_case(case, inputs_root)
        paths = [root/f'seed{seed}'/c['id']/'report.json' for c in plan['carriers']]
        result = audit(inputs_root/f'seed{seed}-inputs', inputs_root/f'seed{seed}-capture', paths)
        resources = {}
        for carrier, report in result['native_reports'].items():
            fingerprints.add(report['catalog']['source_fingerprint'])
            # The runner canonicalizes optional fusion defaults; validate explicit scenario fields.
            actual = report['scenario']
            for name in ('frames','capture_frames','warmup','repeats','seed'):
                if actual[name] != scenario[name]:
                    raise ValueError('scenario differs from frozen budget plan')
            if len(actual['variants']) != len(scenario['variants']):
                raise ValueError('budget scenario variant count mismatch')
            for variant, frozen in zip(actual['variants'], scenario['variants']):
                canonical = dict(diagnostic='color',edge_width_px=24.0,angle_power=2.0,
                                 pyramid_levels=4,smoothness_weight=0.1,**frozen)
                if variant != canonical:
                    raise ValueError('budget profile differs from plan')
            expected = next(c['surface'] for c in plan['carriers'] if
                            c['id'] == {'dome_floor_v1':'dome_floor','cylinder_floor_v1':'cylinder_floor',
                                        'cube_floor_v1':'cube_floor'}.get(carrier,carrier))
            if report['initial_state']['surface'] != expected:
                raise ValueError('carrier geometry differs from frozen budget plan')
            metas = [b['metadata'] for b in report['frame_baselines']]+[s['metadata'] for s in report['samples']]
            values = [check_mesh_budget(m, plan) for m in metas]
            if any(v != values[0] for v in values):
                raise ValueError('carrier allocation changed during run')
            if any([m['width'],m['height']] != plan['output'] for m in metas):
                raise ValueError('budget output dimensions differ')
            resources[carrier] = values[0]
        differences = []
        for row in result['results']:
            reference = next(r for r in result['results'] if r['carrier'] == 'dome_floor_v1'
                             and (r['truth_index'],r['mode'],r['boundary']) ==
                                 (row['truth_index'],row['mode'],row['boundary']))
            differences.append({k:row[k] for k in ('carrier','truth_index','mode','boundary')} |
                {'linear_mae_delta':row['quality']['linear_mae']-reference['quality']['linear_mae'],
                 'target_iou_delta':row['quality']['target']['iou']-reference['quality']['target']['iou']})
        result['limitations'] = [l for l in result['limitations'] if not l.startswith('carriers have unequal')]
        output.append({'seed':seed,'resources':resources,'carrier_minus_dome':differences,'audit':result})
    if len(fingerprints) != 1:
        raise ValueError('mixed native source fingerprints in budget series')
    protocol = repository/'docs/research'/protocol_name
    return {'schema_version':1,'experiment':plan['experiment'],'plan':plan,'provenance':provenance,
            'protocol_sha256':hashlib.sha256(protocol.read_bytes()).hexdigest(),
            'cases':output,'source_fingerprint':fingerprints.pop(),
            'limitations':['near-equal active triangle counts, shared ceilings; not equal total memory or work',
                           'three previously inspected instances of one procedural family; exploratory',
                           'fixed carrier order, no thermal/frequency control; no speed ranking',
                           'true calibration, two-frame clips, coded-object truth only']}


CELL_FIELDS = {'uniform_cells','dome_latitude_cells','dome_longitude_cells',
               'floor_radial_cells','vertical_cells','angular_cells','face_cells'}


def validate_refinement_plan(master):
    levels = master['levels']
    if master['schema_version'] != 1 or [l['id'] for l in levels] != ['coarse','medium','fine']:
        raise ValueError('three ordered refinement levels required')
    medium = levels[1]['plan']
    for level in levels:
        plan = level['plan']
        for key in ('seeds','inputs_plan','scenario','output'):
            if plan[key] != medium[key]:
                raise ValueError('refinement inputs/scenario/output differ')
        if [c['id'] for c in plan['carriers']] != [c['id'] for c in medium['carriers']]:
            raise ValueError('refinement carrier matrix differs')
        for current, reference in zip(plan['carriers'],medium['carriers']):
            shape = lambda s: {k:v for k,v in s.items() if k not in CELL_FIELDS}
            if shape(current['surface']) != shape(reference['surface']):
                raise ValueError('physical carrier shape changed during refinement')
    for before,after in zip(levels,levels[1:]):
        for c,d in zip(before['plan']['carriers'],after['plan']['carriers']):
            a,b = c['surface'],d['surface']
            if a.keys() != b.keys():
                raise ValueError('carrier cell fields differ')
            for key in CELL_FIELDS & a.keys():
                x = a[key] if isinstance(a[key],list) else [a[key]]
                y = b[key] if isinstance(b[key],list) else [b[key]]
                if (not x or len(x) != len(y) or any(type(v) is not int or v <= 0 for v in x+y)
                        or any(j <= i for i,j in zip(x,y))):
                    raise ValueError('all carrier cell axes must strictly increase')


def refinement_difference(actual, reference, roi):
    actual, reference, roi = map(np.asarray,(actual,reference,roi))
    if (actual.dtype != np.uint8 or reference.dtype != np.uint8
            or actual.shape != reference.shape or actual.ndim != 3 or actual.shape[-1] != 4
            or roi.dtype != bool or roi.shape != actual.shape[:2] or not roi.any()
            or not (actual[...,3] == 255).all() or not (reference[...,3] == 255).all()):
        raise ValueError('opaque RGBA8 and nonempty matching boolean ROI required')
    delta = np.abs(actual[...,:3].astype(np.int16)-reference[...,:3].astype(np.int16))
    return {'roi_pixels':int(roi.sum()),
            'full_frame_changed_pixel_fraction':float(np.any(actual != reference,axis=-1).mean()),
            'roi_changed_pixel_fraction':float(np.any(delta,axis=-1)[roi].mean()),
            'roi_max_rgb8_channel_delta':int(delta[roi].max()),
            'roi_linear_mae_to_fine':float(np.abs(linear(actual[...,:3]/255.)-
                                                        linear(reference[...,:3]/255.))[roi].mean())}


def audited_rgba(case, row, run_root):
    native = case['audit']['native_reports'][row['carrier']]
    variant = next(n for n,v in enumerate(native['scenario']['variants']) if
                   (v['mode'],v['pyramid_boundary']) == (row['mode'],row['boundary']))
    sample = next(s for s in native['samples'] if s['frame_index'] == row['frame_index']
                  and s['variant'] == variant and not s['warmup'])
    carrier_id = {'dome_floor_v1':'dome_floor','cylinder_floor_v1':'cylinder_floor',
                  'cube_floor_v1':'cube_floor'}.get(row['carrier'],row['carrier'])
    payload = (run_root/f"seed{case['seed']}"/carrier_id/sample['rgba_file']).read_bytes()
    if hashlib.sha256(payload).hexdigest() != row['rgba_sha256']:
        raise ValueError('refinement RGBA changed after audit')
    meta = sample['metadata']
    return np.frombuffer(payload,np.uint8).reshape(meta['height'],meta['width'],4)


def audit_refinement(plan_path, root, inputs_root):
    repository = Path(__file__).resolve().parents[2]
    plan_path, root, inputs_root = map(Path,(plan_path,root,inputs_root))
    master = json.loads(plan_path.read_text())
    validate_refinement_plan(master)
    provenance = json.loads((root/'provenance.json').read_text())
    if provenance['master_plan_sha256'] != hashlib.sha256(plan_path.read_bytes()).hexdigest():
        raise ValueError('refinement master plan changed after freeze')
    baseline_path = repository/master['medium_baseline']
    if hashlib.sha256(baseline_path.read_bytes()).hexdigest() != master['medium_baseline_sha256']:
        raise ValueError('immutable medium baseline changed')
    baseline = json.loads(baseline_path.read_text())
    levels, run_roots = {}, {}
    for level in master['levels']:
        name = level['id']
        run_root = repository/level['reused_root'] if 'reused_root' in level else root/name
        derived = repository/level['reused_plan'] if 'reused_plan' in level else run_root/'plan.json'
        if json.loads(derived.read_text()) != level['plan']:
            raise ValueError('derived refinement plan differs from master')
        levels[name] = audit_budget(derived,run_root,inputs_root,'CARRIER_REFINEMENT_PROTOCOL.md')
        run_roots[name] = run_root
    if len({l['source_fingerprint'] for l in levels.values()}) != 1:
        raise ValueError('refinement requires matching native implementations')
    for current,previous in zip(levels['medium']['cases'],baseline['cases']):
        if (current['seed'] != previous['seed'] or any(current['audit'][k] != previous['audit'][k]
                for k in ('native_report_sha256','native_config_sha256','fixture_sha256','capture_sha256'))):
            raise ValueError('reused medium inputs/reports differ from pinned baseline')
    comparisons = []
    for index,medium in enumerate(levels['medium']['cases']):
        seed = medium['seed']
        _,_,_,rois,_,_ = load_sequence(inputs_root/f'seed{seed}-inputs',inputs_root/f'seed{seed}-capture','any')
        cases = {name:l['cases'][index] for name,l in levels.items()}
        for row in medium['audit']['results']:
            key = lambda r: tuple(r[k] for k in ('carrier','truth_index','mode','boundary'))
            rows = {name:next(r for r in c['audit']['results'] if key(r) == key(row))
                    for name,c in cases.items()}
            resources = [cases[n]['resources'][row['carrier']] for n in ('coarse','medium','fine')]
            if any(b['triangles'] <= a['triangles'] or b['buffer_bytes'] <= a['buffer_bytes']
                   for a,b in zip(resources,resources[1:])):
                raise ValueError('actual refinement resources must strictly increase')
            images = {name:audited_rgba(c,rows[name],run_roots[name]) for name,c in cases.items()}
            result = dict(seed=seed,**{k:row[k] for k in ('carrier','truth_index','mode','boundary')})
            for before,after in (('coarse','medium'),('medium','fine')):
                a,b = rows[before]['quality'],rows[after]['quality']
                result[after+'_minus_'+before] = dict(linear_mae_delta=b['linear_mae']-a['linear_mae'],
                    target_iou_delta=b['target']['iou']-a['target']['iou'])
            for name in ('coarse','medium'):
                result[name+'_to_fine'] = refinement_difference(images[name],images['fine'],rois[row['truth_index']])
            comparisons.append(result)
    # Preserve audited metrics and hash references without duplicating full native reports in Git.
    for level in levels.values():
        for case in level['cases']:
            del case['audit']['native_reports']
        level['limitations'] = ['within-carrier refinement; cross-carrier equal budgets not asserted',
                                'raw native reports and RGBA remain in artifacts for complete re-audit']
    protocol = repository/'docs/research/CARRIER_REFINEMENT_PROTOCOL.md'
    return {'schema_version':1,'experiment':master['experiment'],'plan':master,'provenance':provenance,
            'protocol_sha256':hashlib.sha256(protocol.read_bytes()).hexdigest(),
            'analyzer_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'source_fingerprint':levels['medium']['source_fingerprint'],
            'levels':levels,'comparisons':comparisons,
            'limitations':['finite fine reference is not scene truth or asymptotic convergence',
                           'previously inspected street instances, two adjacent frames, coded-target truth',
                           'fixed level/carrier order, no timing ranking or full memory equality']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path)
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--report',type=Path,action='append')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--comparison',choices=('pyramid_boundary','seam_solver'),default='pyramid_boundary')
    parser.add_argument('--refinement-plan',type=Path)
    parser.add_argument('--refinement-root',type=Path)
    parser.add_argument('--budget-plan',type=Path)
    parser.add_argument('--budget-root',type=Path)
    parser.add_argument('--inputs-root',type=Path)
    parser.add_argument('--study-plan',type=Path)
    parser.add_argument('--study-root',type=Path)
    args = parser.parse_args()
    if args.refinement_plan:
        if not args.refinement_root or not args.inputs_root or args.budget_plan or args.study_plan or args.fixture or args.report or args.capture:
            parser.error('refinement audit requires --refinement-root and --inputs-root only')
        result = audit_refinement(args.refinement_plan,args.refinement_root,args.inputs_root)
    elif args.budget_plan:
        if not args.budget_root or not args.inputs_root or args.study_plan or args.fixture or args.report or args.capture:
            parser.error('budget audit requires --budget-root and --inputs-root only')
        result = audit_budget(args.budget_plan,args.budget_root,args.inputs_root)
    elif args.study_plan:
        if not args.study_root or args.fixture or args.report or args.capture:
            parser.error('study requires --study-root and excludes single-fixture arguments')
        result = audit_study(args.study_plan,args.study_root)
    else:
        if not args.fixture or not args.report or args.study_root:
            parser.error('single audit requires --fixture and --report')
        result = audit(args.fixture,args.capture or args.fixture,args.report,args.comparison)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    reports = ([c['audit'] for l in result['levels'].values() for c in l['cases']] if 'levels' in result
               else [c['audit'] for c in result['cases']] if 'cases' in result else [result])
    print(json.dumps({'conditions':sum(len(r['results']) for r in reports),
                      'pairs':sum(len(r['paired_differences']) for r in reports)}))
