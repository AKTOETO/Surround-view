"""Controlled target-height/range study over native server research reports."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools'), str(Path(__file__).resolve().parent)]
from carrier_lateral import run as native_run
from object_metrics import remove_small, target_mask, measure_visible_target
from server_boundary import audit_budget, audited_rgba
from temporal_seam_stability import _verified, load_sequence


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def ray_ground(center, point):
    """Hypothetical ray through a representative point, not box-surface correspondence."""
    center, point = np.asarray(center, float), np.asarray(point, float)
    if center.shape != (3,) or point.shape != (3,) or not np.isfinite([center, point]).all() or center[2] <= 0:
        raise ValueError('finite 3D center above ground and point required')
    denominator = center[2]-point[2]
    if abs(denominator) <= 1e-12:
        return {'status': 'parallel', 't': None, 'intersection_m': None, 'xy_displacement_m': None}
    t = float(center[2]/denominator)
    if t <= 0:
        return {'status': 'behind', 't': t, 'intersection_m': None, 'xy_displacement_m': None}
    intersection = center+t*(point-center)
    return {'status': 'forward', 't': t, 'intersection_m': intersection.tolist(),
            'xy_displacement_m': float(np.linalg.norm((intersection-point)[:2]))}


def object_position(rgb, truth, threshold, minimum):
    """Component-filtered full-frame centroid proxy; missing observations stay undefined."""
    predicted, _ = remove_small(target_mask(rgb, threshold), minimum)
    expected, _ = remove_small(truth, minimum)
    def centroid(mask):
        yy, xx = np.nonzero(mask)
        return [float(xx.mean()), float(yy.mean())] if xx.size else None
    actual, reference = centroid(predicted), centroid(expected)
    return {'predicted_centroid_px': actual, 'truth_centroid_px': reference,
            'centroid_distance_px': float(np.linalg.norm(np.asarray(actual)-reference))
                if actual is not None and reference is not None else None}


def validate_factors(asset):
    if asset['schema_version'] == 2:
        if asset.get('rig_seeds') != [101,104] or asset.get('projection_margin_px') != 8:
            raise ValueError('frozen two-rig design and projection margin required')
        for seed in asset['rig_seeds']:
            group = [json.loads(json.dumps(c)) for c in asset['cases'] if c['scenario']['seed']==seed]
            for c in group:
                prefix = f'rig{seed}-'
                if not c['id'].startswith(prefix):
                    raise ValueError('case ID must identify rig seed')
                c['id'] = c['id'][len(prefix):]
            validate_factors({'schema_version':1,'ground_size_m':asset['ground_size_m'],'cases':group})
        if len(asset['cases']) != 12:
            raise ValueError('complete two-rig factor matrix required')
        normalized = []
        for c in asset['cases']:
            if set(c['virtual_camera']) != {'azimuth_rad','elevation_rad','distance_m','fov_y_rad','clip_m'}:
                raise ValueError('framed study requires server-supported view fields')
            value = json.loads(json.dumps(c))
            del value['id'];del value['target']['center_m'];del value['scenario']['seed']
            normalized.append(value)
        if any(c != normalized[0] for c in normalized):
            raise ValueError('only rig seed and target position may differ')
        return
    expected = {(name,z) for name in ('near','far') for z in (.2,.8,1.4)}
    if ({(c['id'].split('-')[0], c['target']['center_m'][2]) for c in asset['cases']} != expected
            or len(asset['cases']) != 6 or asset['ground_size_m'] != [100,100]):
        raise ValueError('complete frozen height/range matrix required')
    reference = None
    for case in asset['cases']:
        xy = [2.4,2.3] if case['id'].startswith('near-') else [4.8,4.6]
        if case['target']['center_m'][:2] != xy or case['target']['size_m'] != [.4,.4,.4]:
            raise ValueError('fixed range and box size required')
        control = json.loads(json.dumps(case))
        del control['id']
        del control['target']['center_m']
        if reference is not None and control != reference:
            raise ValueError('uncontrolled case parameter differs')
        reference = control


def box_frame_bounds(case, vehicle_pose):
    """Conservative full-box pinhole bounds; no Blender/object visibility needed."""
    from itertools import product
    view = case['virtual_camera']
    target = np.asarray(view.get('target_m',[0.,0.,0.]))
    az, el = view['azimuth_rad'], view['elevation_rad']
    eye = target+view['distance_m']*np.array([np.cos(el)*np.cos(az),np.cos(el)*np.sin(az),np.sin(el)])
    forward = target-eye;forward /= np.linalg.norm(forward)
    right = np.cross(forward,[0.,0.,1.])
    if np.linalg.norm(right) < 1e-6: right = np.cross(forward,[0.,1.,0.])
    right /= np.linalg.norm(right)
    optical = np.eye(4);optical[:3,:3] = np.column_stack([right,np.cross(right,forward),-forward]);optical[:3,3] = eye
    center, size = np.asarray(case['target']['center_m']), np.asarray(case['target']['size_m'])
    corners = np.array([[*(center+size*np.asarray(sign)/2),1.] for sign in product((-1,1),repeat=3)])
    camera = corners @ np.linalg.inv(np.asarray(vehicle_pose) @ optical).T
    depth = -camera[:,2]
    near, far = view['clip_m']
    if np.any(depth <= near) or np.any(depth >= far):
        raise ValueError('target box intersects near/far clipping')
    w,h = case['capture']['width'],case['capture']['height']
    tangent = np.tan(view['fov_y_rad']/2)
    x = (camera[:,0]/depth/(tangent*w/h)+1)*w/2-.5
    y = (1-camera[:,1]/depth/tangent)*h/2-.5
    bounds = [float(x.min()),float(y.min()),float(x.max()),float(y.max())]
    # Pixel footprint borders are -.5 and dimension-.5.
    margin = min(bounds[0]+.5,bounds[1]+.5,w-.5-bounds[2],h-.5-bounds[3])
    return {'bounds_px':bounds,'minimum_margin_px':float(margin),'depth_range_m':[float(depth.min()),float(depth.max())]}


def verify_rig_pair(a, b):
    """Only extrinsics may change; view, intrinsics and trajectory stay fixed."""
    acfg, astamps, aposes = a
    bcfg, bstamps, bposes = b
    def without_extrinsics(cfg):
        result = json.loads(json.dumps(cfg))
        for camera in result['cameras']:
            camera.pop('T_camera_from_vehicle')
        return result
    if (acfg['cameras'] == bcfg['cameras'] or astamps != bstamps or aposes != bposes
            or without_extrinsics(acfg) != without_extrinsics(bcfg)):
        raise ValueError('rigs must differ only in camera extrinsics on identical trajectory/view')


def plans(master_path, inputs):
    plan = json.loads(master_path.read_text())
    asset_path = ROOT/plan['inputs_plan']
    asset = json.loads(asset_path.read_text())
    provenance = json.loads((inputs/'provenance.json').read_text())
    if provenance['master_plan_sha256'] != sha(asset_path):
        raise ValueError('controlled capture master changed')
    for name,digest in provenance['generator_sha256'].items():
        if sha(ROOT/'tools/blender'/name) != digest:
            raise ValueError('controlled capture helper changed')
    validate_factors(asset)
    if json.loads((inputs/'progress.json').read_text()).get('state') != 'complete':
        raise ValueError('capture matrix is not complete')
    result = []
    for case in asset['cases']:
        directory = inputs/case['id']
        local = directory/'plan.json'
        if json.loads(local.read_text()) != {'schema_version': 1, 'cases': [case]}:
            raise ValueError('derived capture case differs from master')
        local_provenance = json.loads((directory/'provenance.json').read_text())
        if (local_provenance['master_plan_sha256'] != sha(asset_path)
                or local_provenance['plan_sha256'] != sha(local)
                or local_provenance['world_override'] != 'flat_road_ego_target_v1'):
            raise ValueError('controlled world provenance differs')
        input_path = str(local.relative_to(ROOT)) if local.is_relative_to(ROOT) else str(local)
        result.append((case, dict(plan, inputs_plan=input_path,seeds=[case['scenario']['seed']])))
    return plan, asset, result


def run(master_path, root, inputs, binaries):
    plan, _, cases = plans(master_path, inputs)
    root.mkdir(parents=True, exist_ok=False)
    write(root/'provenance.json', {'master_plan_sha256': sha(master_path),
          'inputs_master_sha256': sha(ROOT/plan['inputs_plan'])})
    for case, derived in cases:
        directory = root/case['id']
        directory.mkdir()
        path = directory/'native-plan.json'
        write(path, derived)
        native_run(path, directory/'native', inputs/case['id'], binaries)


def audit(master_path, root, inputs):
    plan, asset, cases = plans(master_path, inputs)
    provenance = json.loads((root/'provenance.json').read_text())
    if (provenance['master_plan_sha256'] != sha(master_path)
            or provenance['inputs_master_sha256'] != sha(ROOT/plan['inputs_plan'])):
        raise ValueError('native study master differs')
    output, controls, fingerprints, devices = [], [], set(), set()
    references = {}
    direct_references, rig_controls = {}, []
    framed = asset['schema_version'] == 2
    for case, derived in cases:
        cid = case['id']
        path = root/cid/'native-plan.json'
        if json.loads(path.read_text()) != derived:
            raise ValueError('native case plan differs from master')
        result = audit_budget(path, root/cid/'native', inputs/cid, plan.get('protocol','PARALLAX_HEIGHT_PROTOCOL.md'))
        native_case = result['cases'][0]
        if framed:
            for report in native_case['audit']['native_reports'].values():
                catalog = report['catalog']
                devices.add(tuple(catalog[k] for k in ('backend','gl_vendor','gl_renderer')))
            result['limitations'] = [l for l in result['limitations'] if not l.startswith('three previously')]
            result['limitations'].append('one fully framed factor of two seeded mounts; not scene-family holdout')
            native_case['audit']['limitations'] = [l for l in native_case['audit']['limitations']
                                                  if l != 'exploratory reused fixture; not holdout']
            native_case['audit']['limitations'].append('new controlled flat-world factor; not independent holdout')
        seed = case['scenario']['seed']
        dataset, capture = inputs/cid/f'seed{seed}-inputs', inputs/cid/f'seed{seed}-capture'
        cfg, images, direct_rgb, _, stamps, poses = load_sequence(dataset, capture, 'any')
        if framed:
            factor = cid.split('-',1)[1]
            digest = hashlib.sha256(np.asarray(direct_rgb).tobytes()).hexdigest()
            if factor in direct_references:
                if direct_references[factor] != digest:
                    raise ValueError('independent direct RGB changed across mount-only factor')
                rig_controls.append({'factor':factor,'seeds':asset['rig_seeds'],
                                     'decoded_direct_rgb_sha256':digest,'equal':True})
            else:
                direct_references[factor] = digest
        context = (cfg, stamps, poses)
        if seed in references and context != references[seed]:
            raise ValueError('rig/view/trajectory changed between factors')
        references[seed] = context
        truth = json.loads((capture/'paired_truth.json').read_text())
        info = json.loads((capture/'capture.json').read_text())
        inventory = json.loads((inputs/cid/'provenance.json').read_text())['retained_meshes']
        if sorted(truth['objects']) != inventory or info['near_obstacles'] != []:
            raise ValueError('controlled world inventory differs')
        target_id = truth['objects'][truth['diagnostic_target']['object_name']]
        targets, visibilities = [], []
        for t, frame in enumerate(truth['frames']):
            # Legacy static camera capture has no evaluated target field. The
            # independent truth export records it; generator hashes bind both.
            if not np.allclose(frame['diagnostic_target_position_m'], case['target']['center_m'], rtol=0, atol=1e-5):
                raise ValueError('evaluated target position differs from factor')
            labels = np.load(_verified(capture,frame['objects'],truth['sha256']),allow_pickle=False)
            visibility = np.load(_verified(capture,frame['visibility'],truth['sha256']),allow_pickle=False)
            targets.append(labels == target_id)
            visibilities.append(visibility != 0)
            point = np.linalg.inv(np.asarray(poses[t])) @ [*case['target']['center_m'], 1]
            camera_rays = []
            for camera in cfg['cameras']:
                center = np.linalg.inv(camera['T_camera_from_vehicle'])[:3,3]
                camera_rays.append({'camera_id': camera['id'], 'center_m': center.tolist(),
                                    **ray_ground(center,point[:3])})
            controls.append({'case': cid, 'truth_index': t, 'point_vehicle_m': point[:3].tolist(),
                             'camera_rays': camera_rays, 'direct_target_pixels': int(targets[-1].sum()),
                             'direct_target_any_camera_visible_pixels': int((targets[-1] & (visibility != 0)).sum()),
                             'input_chroma_pixels': [int(target_mask(image/255.,case['target']['chroma_threshold']).sum())
                                                     for image in images[t]]})
            if framed:
                bounds = box_frame_bounds(case,poses[t])
                if bounds['minimum_margin_px'] < asset['projection_margin_px']:
                    raise ValueError('full target box violates frozen projection margin')
                yy,xx = np.nonzero(targets[-1])
                x0,y0,x1,y1 = bounds['bounds_px']
                if xx.size and (xx.min()<x0-1e-3 or xx.max()>x1+1e-3 or yy.min()<y0-1e-3 or yy.max()>y1+1e-3):
                    raise ValueError('independent target IDs exceed analytic full-box projection')
                controls[-1]['projected_box'] = bounds
        for row in native_case['audit']['results']:
            rgba = audited_rgba(native_case,row,root/cid/'native')
            row['object_position'] = object_position(rgba[...,:3]/255.,targets[row['truth_index']],
                case['target']['chroma_threshold'],case['target']['min_component_pixels'])
            if framed:
                row['visible_target'] = measure_visible_target(target_mask(rgba[...,:3],case['target']['chroma_threshold']),
                    targets[row['truth_index']],visibilities[row['truth_index']],case['target']['min_component_pixels'])
                if row['visible_target']['touches_frame_border']:
                    raise ValueError('direct target touches frame border despite full-box preflight')
        fingerprints.add(result['source_fingerprint'])
        del native_case['audit']['native_reports']
        output.append({'id': cid, 'target_center_m': case['target']['center_m'],
                       'capture_provenance_sha256': sha(inputs/cid/'provenance.json'),
                       'native_plan_sha256': sha(path), 'audit': result})
        if framed:
            output[-1]['mount_offsets'] = info['mount_offsets']
    if len(fingerprints) != 1:
        raise ValueError('mixed native source implementations')
    if framed:
        a,b = (references[s] for s in asset['rig_seeds'])
        verify_rig_pair(a,b)
    summary = {'schema_version': 1, 'experiment': plan['experiment'], 'plan': plan,
            'asset': asset, 'provenance': provenance, 'cases': output, 'ray_ground_controls': controls,
            'source_fingerprint': fingerprints.pop(), 'analyzer_sha256': sha(Path(__file__)),
            'limitations': [('one flat world, two mount seeds, two dependent poses; not independent scene-family holdout'
                              if framed else 'one controlled world/rig, two dependent poses; not independent scene holdout'),
                            'box-center rays are hypothetical, not visible-surface correspondences',
                            'centroids may hide multiple copies; no natural-object ghost segmentation',
                            # Preserve legacy baseline text for reproduction;
                            # its Mesa label is erroneous (see validation erratum).
                            ('fixed order, CPU fusion with reported GLES renderer; no sustained FPS or target conclusion'
                             if framed else 'fixed order, CPU/Mesa paused trials; no sustained FPS or target conclusion')]}
    if framed:
        if len(devices) != 1:
            raise ValueError('mixed rendering devices in framed study')
        summary['render_device'] = dict(zip(('backend','gl_vendor','gl_renderer'),devices.pop()))
        summary['mount_pair_direct_controls'] = rig_controls
    return summary



def verify_sampling_design(low_plan, high_plan, low_asset, high_asset):
    """Reject every plan change except the predeclared cubemap resolution."""
    plans_copy = json.loads(json.dumps([low_plan, high_plan]))
    for plan in plans_copy:
        for field in ('experiment', 'inputs_plan', 'protocol'):
            del plan[field]
    assets_copy = json.loads(json.dumps([low_asset, high_asset]))
    for asset, size in zip(assets_copy, (256, 512)):
        validate_factors(asset)
        for case in asset['cases']:
            if case['capture'].pop('face_size') != size:
                raise ValueError('sampling requires exactly 256 and 512 cubemap faces')
    if plans_copy[0] != plans_copy[1] or assets_copy[0] != assets_copy[1]:
        raise ValueError('sampling design changed a non-resolution factor')


def sampling_rows(summary):
    rows = {}
    for case in summary['cases']:
        for row in case['audit']['cases'][0]['audit']['results']:
            key = (case['id'], *(row[k] for k in ('carrier','truth_index','mode','boundary')))
            if key in rows:
                raise ValueError('duplicate sampling condition')
            rows[key] = row
    return rows


def pair_sampling_rows(low, high):
    """Match by semantic key, never by report ordering; undefined remains null."""
    a, b = sampling_rows(low), sampling_rows(high)
    if a.keys() != b.keys():
        raise ValueError('sampling conditions differ')
    def metrics(row):
        return {'full_iou':row['quality']['target']['iou'],
                'conditional_iou':row['visible_target']['conditional_iou'],
                'predicted_pixels':row['quality']['target']['predicted_pixels'],
                'centroid_distance_px':row['object_position']['centroid_distance_px'],
                'linear_mae':row['quality']['linear_mae']}
    result = []
    for key in sorted(a):
        if a[key]['scenario_timestamp_ns'] != b[key]['scenario_timestamp_ns']:
            raise ValueError('sampling timestamps differ')
        x, y = metrics(a[key]), metrics(b[key])
        result.append(dict(zip(('case','carrier','truth_index','mode','boundary'),key)) |
                      {'low_rgba_sha256':a[key]['rgba_sha256'],'high_rgba_sha256':b[key]['rgba_sha256'],
                       'low':x,'high':y,'delta':{k:y[k]-x[k] if x[k] is not None and y[k] is not None
                                                else None for k in x}})
    return result


def sampling_followup(current, baseline_path, low_root, low_inputs, high_root, high_inputs):
    from PIL import Image
    historical = json.loads(baseline_path.read_text())
    low = audit(ROOT/'configs/research/parallax-framed-plan.json',low_root,low_inputs)
    verify_historical_results(low,historical)
    verify_sampling_design(low['plan'],current['plan'],low['asset'],current['asset'])
    if (low['source_fingerprint'] != current['source_fingerprint']
            or low['render_device'] != current['render_device']):
        raise ValueError('sampling implementation or rendering device changed')
    controls = []
    for case in low['asset']['cases']:
        cid, seed = case['id'], case['scenario']['seed']
        contexts, truths, metadata = [], [], []
        binary_digests = []
        checked_faces = 0
        for inputs, root, size in ((low_inputs,low_root,256),(high_inputs,high_root,512)):
            capture = inputs/cid/f'seed{seed}-capture'
            dataset = inputs/cid/f'seed{seed}-inputs'
            cfg, images, direct, _, stamps, poses = load_sequence(dataset,capture,'any')
            contexts.append((cfg,stamps,poses))
            truth = json.loads((capture/'paired_truth.json').read_text())
            # Compare all decoded truth pixels and raw visibility camera bits.
            digest = hashlib.sha256(np.asarray(direct).tobytes())
            for frame in truth['frames']:
                for key in ('objects','visibility'):
                    value = np.load(_verified(capture,frame[key],truth['sha256']),allow_pickle=False)
                    digest.update(str((value.shape,value.dtype.str)).encode());digest.update(value.tobytes())
            truths.append(digest.hexdigest())
            info = json.loads((capture/'capture.json').read_text())
            if info['face_size'] != size:
                raise ValueError('capture resolution differs from sampling design')
            for frame in info['frames']:
                for camera in frame['cameras']:
                    for name in camera['faces'].values():
                        checked_faces += 1
                        with Image.open(_verified(capture,name,info['sha256'])) as image:
                            if image.size != (size,size):
                                raise ValueError('actual cubemap dimensions differ')
            # Strict equality of capture controls; only files/face size differ.
            metadata.append({k:v for k,v in info.items() if k not in ('face_size','sha256')})
            binary_digests.append([json.loads((root/cid/f'native/seed{seed}'/c['id']/'invocation.json')
                                               .read_text())['binary_sha256'] for c in low['plan']['carriers']])
            if any(image.shape != (400,400,3) for frame in images for image in frame):
                raise ValueError('fisheye input dimensions changed')
        if (contexts[0] != contexts[1] or truths[0] != truths[1]
                or metadata[0] != metadata[1] or binary_digests[0] != binary_digests[1]):
            raise ValueError('sampling controls differ: config/trajectory/truth/capture/binaries')
        controls.append({'case':cid,'decoded_truth_sha256':truths[0],
                         'identical_controls':True,'checked_cube_faces':checked_faces})
    results = pair_sampling_rows(low,current)
    if len(results) != 480:
        raise ValueError('complete 480-condition sampling comparison required')
    return {'schema_version':1,'experiment':'E-STITCH-parallax-sampling-paired-01',
            'baseline_sha256':sha(baseline_path),'low_plan':low['plan'],'high_plan':current['plan'],
            'low_provenance':low['provenance'],'high_provenance':current['provenance'],
            'source_fingerprint':current['source_fingerprint'],'render_device':current['render_device'],
            'analyzer_sha256':sha(Path(__file__)),
            'native_report_sha256':{level:{c['id']:c['audit']['cases'][0]['audit']['native_report_sha256']
                                          for c in summary['cases']}
                                    for level,summary in (('low',low),('high',current))},
            'controls':controls,'results':results,
            'limitations':['only cubemap 256 to 512; fixed 400px fisheye and 320x180 output',
                           'two mounts of one flat scene, two dependent poses; no universal carrier ranking',
                           'persistent error does not isolate all geometric and sampling contributions',
                           'timings not compared; desktop NVIDIA, not target Aurora']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--run-binaries', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--visibility-baseline', type=Path,
                        help='Verify this historical full-mask baseline and emit conditional visibility follow-up')
    parser.add_argument('--sampling-baseline', type=Path, help='Reaudit and pair the frozen 256px framed baseline')
    parser.add_argument('--reference-root', type=Path)
    parser.add_argument('--reference-inputs', type=Path)
    parser.add_argument('--output-resolution-pair', action='store_true', help='Verify frozen reference and pair output resolutions')
    parser.add_argument('--sensor-resolution-pair', action='store_true', help='Verify frozen reference and pair fisheye resolutions')
    args = parser.parse_args()
    if args.sensor_resolution_pair and (args.visibility_baseline or args.sampling_baseline or args.output_resolution_pair):
        parser.error('sensor-resolution comparison excludes other follow-ups')
    if args.output_resolution_pair and (args.visibility_baseline or args.sampling_baseline):
        parser.error('output-resolution comparison excludes other follow-ups')
    if args.sampling_baseline and (args.visibility_baseline or not args.reference_root or not args.reference_inputs):
        parser.error('sampling comparison requires reference root/inputs and excludes visibility follow-up')
    if args.visibility_baseline and args.run_binaries:
        parser.error('--visibility-baseline reuses historical trials; omit --run-binaries')
    master, root, inputs = (p.resolve() for p in (args.plan,args.root,args.inputs))
    if args.run_binaries:
        run(master,root,inputs,args.run_binaries.resolve())
    result = audit(master,root,inputs)
    if args.visibility_baseline:
        result = visibility_followup(result,args.visibility_baseline.resolve(),root,inputs)
    if args.sampling_baseline:
        result = sampling_followup(result, args.sampling_baseline.resolve(),
                                   args.reference_root.resolve(), args.reference_inputs.resolve(), root, inputs)
    if args.output_resolution_pair:
        from parallax_output import output_followup
        result = output_followup(result,root,inputs)
    if args.sensor_resolution_pair:
        from sensor_sampling import sensor_followup
        result = sensor_followup(result,root,inputs)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    write(args.output,result)
    print('Audited',len(result['results']) if 'results' in result else
          sum(len(c['audit']['cases'][0]['audit']['results']) for c in result['cases']),'quality conditions')


def verify_historical_results(current, historical):
    # Extending the metric module changes its file digest, even though the old
    # metrics are unchanged. Exempt exactly these two code digests; compare all
    # numerical values, inputs, native hashes and other code/protocol digests.
    a, b = json.loads(json.dumps(current)), json.loads(json.dumps(historical))
    for value in (a,b):
        value.pop('analyzer_sha256')
        for entry in value['cases']:
            entry['audit']['cases'][0]['audit']['metric_code_sha256'].pop('object_metrics.py')
    if a != b:
        raise ValueError('historical full-mask baseline does not reproduce')


def visibility_followup(current, baseline_path, root, inputs):
    historical = json.loads(baseline_path.read_text())
    verify_historical_results(current,historical)
    results = []
    for entry in current['cases']:
        cid = entry['id']
        case = entry['audit']['cases'][0]
        capture = inputs/cid/'seed101-capture'
        truth = json.loads((capture/'paired_truth.json').read_text())
        target = truth['diagnostic_target']
        masks = []
        for frame in truth['frames']:
            labels = np.load(_verified(capture,frame['objects'],truth['sha256']),allow_pickle=False)
            bits = np.load(_verified(capture,frame['visibility'],truth['sha256']),allow_pickle=False)
            masks.append((labels == truth['objects'][target['object_name']],bits != 0))
        # audit() intentionally compacts reports; restore them only after checking
        # their frozen digest, for the shared audited RGBA reader.
        reports = {}
        aliases = {'dome_floor_v1':'dome_floor','cylinder_floor_v1':'cylinder_floor','cube_floor_v1':'cube_floor'}
        for carrier,digest in case['audit']['native_report_sha256'].items():
            path = root/cid/'native/seed101'/aliases.get(carrier,carrier)/'report.json'
            if sha(path) != digest:
                raise ValueError('native report changed after audit')
            reports[carrier] = json.loads(path.read_text())
        case['audit']['native_reports'] = reports
        for row in case['audit']['results']:
            rgba = audited_rgba(case,row,root/cid/'native')
            expected, visible = masks[row['truth_index']]
            metric = measure_visible_target(target_mask(rgba[...,:3],target['chroma_threshold']),
                                            expected,visible,target['min_component_pixels'])
            results.append({k:row[k] for k in ('carrier','truth_index','frame_index','mode','boundary','rgba_sha256')} |
                           {'case':cid,'full_mask_iou':row['quality']['target']['iou'],'visible_target':metric})
    protocol = ROOT/'docs/research/PARALLAX_VISIBILITY_PROTOCOL.md'
    return {'schema_version':1,'experiment':'E-STITCH-parallax-visibility-01',
            'baseline_sha256':sha(baseline_path),'protocol_sha256':sha(protocol),
            'analyzer_sha256':sha(Path(__file__)),'metric_code_sha256':sha(ROOT/'tools/object_metrics.py'),
            'historical_analyzer_sha256':historical['analyzer_sha256'],
            'historical_metric_code_sha256':historical['cases'][0]['audit']['cases'][0]['audit']['metric_code_sha256'],
            'source_fingerprint':current['source_fingerprint'],'results':results,
            'limitations':['same native outputs; no new trials or independent holdout',
                           'visibility is direct-surface ray support, not pixel correspondence',
                           'frame-border contact flags possible clipping; does not prove full object extent']}


if __name__ == '__main__':
    main()
