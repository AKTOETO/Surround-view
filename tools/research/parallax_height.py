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
from object_metrics import remove_small, target_mask
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
        result.append((case, dict(plan, inputs_plan=input_path)))
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
    output, controls, fingerprints = [], [], set()
    reference = None
    for case, derived in cases:
        cid = case['id']
        path = root/cid/'native-plan.json'
        if json.loads(path.read_text()) != derived:
            raise ValueError('native case plan differs from master')
        result = audit_budget(path, root/cid/'native', inputs/cid, 'PARALLAX_HEIGHT_PROTOCOL.md')
        native_case = result['cases'][0]
        seed = case['scenario']['seed']
        dataset, capture = inputs/cid/f'seed{seed}-inputs', inputs/cid/f'seed{seed}-capture'
        cfg, images, _, _, stamps, poses = load_sequence(dataset, capture, 'any')
        context = (cfg, stamps, poses)
        if reference is not None and context != reference:
            raise ValueError('rig/view/trajectory changed between factors')
        reference = context
        truth = json.loads((capture/'paired_truth.json').read_text())
        info = json.loads((capture/'capture.json').read_text())
        inventory = json.loads((inputs/cid/'provenance.json').read_text())['retained_meshes']
        if sorted(truth['objects']) != inventory or info['near_obstacles'] != []:
            raise ValueError('controlled world inventory differs')
        target_id = truth['objects'][truth['diagnostic_target']['object_name']]
        targets = []
        for t, frame in enumerate(truth['frames']):
            # Legacy static camera capture has no evaluated target field. The
            # independent truth export records it; generator hashes bind both.
            if not np.allclose(frame['diagnostic_target_position_m'], case['target']['center_m'], rtol=0, atol=1e-5):
                raise ValueError('evaluated target position differs from factor')
            labels = np.load(_verified(capture,frame['objects'],truth['sha256']),allow_pickle=False)
            visibility = np.load(_verified(capture,frame['visibility'],truth['sha256']),allow_pickle=False)
            targets.append(labels == target_id)
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
        for row in native_case['audit']['results']:
            rgba = audited_rgba(native_case,row,root/cid/'native')
            row['object_position'] = object_position(rgba[...,:3]/255.,targets[row['truth_index']],
                case['target']['chroma_threshold'],case['target']['min_component_pixels'])
        fingerprints.add(result['source_fingerprint'])
        del native_case['audit']['native_reports']
        output.append({'id': cid, 'target_center_m': case['target']['center_m'],
                       'capture_provenance_sha256': sha(inputs/cid/'provenance.json'),
                       'native_plan_sha256': sha(path), 'audit': result})
    if len(fingerprints) != 1:
        raise ValueError('mixed native source implementations')
    return {'schema_version': 1, 'experiment': plan['experiment'], 'plan': plan,
            'asset': asset, 'provenance': provenance, 'cases': output, 'ray_ground_controls': controls,
            'source_fingerprint': fingerprints.pop(), 'analyzer_sha256': sha(Path(__file__)),
            'limitations': ['one controlled world/rig, two dependent poses; not independent scene holdout',
                            'box-center rays are hypothetical, not visible-surface correspondences',
                            'centroids may hide multiple copies; no natural-object ghost segmentation',
                            'fixed order, CPU/Mesa paused trials; no sustained FPS or target conclusion']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--run-binaries', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    master, root, inputs = (p.resolve() for p in (args.plan,args.root,args.inputs))
    if args.run_binaries:
        run(master,root,inputs,args.run_binaries.resolve())
    result = audit(master,root,inputs)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    write(args.output,result)
    print('Audited',sum(len(c['audit']['cases'][0]['audit']['results']) for c in result['cases']),'quality conditions')


if __name__ == '__main__':
    main()
