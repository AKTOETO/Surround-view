"""Controlled height/range captures without changing frozen legacy helpers."""
import hashlib
import json
from pathlib import Path

import bpy

from diagnostic import build_diagnostic
from paired_truth import capture_paired


def capture_study(plan_path, output):
    plan_path, output = Path(plan_path).resolve(), Path(output).resolve()
    plan = json.loads(plan_path.read_text())
    output.mkdir(parents=True, exist_ok=False)
    original = bpy.context.window.scene
    helpers = ('scene.py', 'scenario.py', 'rig.py', 'diagnostic.py', 'diagnostic_motion.py',
               'paired_truth.py', 'visibility.py', 'geometry_truth.py', 'parallax_study.py')
    provenance = {'master_plan_sha256': hashlib.sha256(plan_path.read_bytes()).hexdigest(),
                  'generator_sha256': {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                       for name in helpers}, 'blender_version': bpy.app.version_string}
    (output/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    try:
        for case in plan['cases']:
            scene = build_diagnostic(case)
            bpy.context.window.scene = scene
            ego = scene.objects[scene['sv_ego']]
            target = json.loads(scene['sv_diagnostic_target'])['object_name']
            keep = {ego, *ego.children_recursive, scene.objects[target]}
            road = next(obj for obj in scene.objects if obj.name.split('.')[0] == 'SV road')
            keep.add(road)
            for obj in list(scene.objects):
                if obj.type == 'MESH' and obj not in keep:
                    bpy.data.objects.remove(obj, do_unlink=True)
            road.scale.y = plan['ground_size_m'][1]/12.
            road.scale.x = plan['ground_size_m'][0]/100.
            scene['sv_near_obstacles'] = '[]'
            for key in ('sv_config', 'sv_nominal_config'):
                cfg = json.loads(scene[key])
                cfg['virtual_camera'] = case['virtual_camera']
                scene[key] = json.dumps(cfg)
            directory = output/case['id']
            directory.mkdir()
            derived = directory/'plan.json'
            derived.write_text(json.dumps({'schema_version': 1, 'cases': [case]}, indent=2)+'\n')
            local = dict(provenance, plan_sha256=hashlib.sha256(derived.read_bytes()).hexdigest(),
                         world_override='flat_road_ego_target_v1',
                         retained_meshes=sorted(o.name for o in scene.objects if o.type == 'MESH'))
            (directory/'provenance.json').write_text(json.dumps(local, indent=2)+'\n')
            (output/'progress.json').write_text(json.dumps({'case': case['id'], 'state': 'capturing'}))
            capture_paired(scene, directory/f"seed{case['scenario']['seed']}-capture", **case['capture'])
        (output/'progress.json').write_text(json.dumps({'state': 'complete'}))
    except Exception as error:
        (output/'progress.json').write_text(json.dumps({'state': 'failed', 'error': str(error)}))
        raise
    finally:
        bpy.context.window.scene = original
    return str(output)
