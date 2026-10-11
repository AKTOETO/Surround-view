"""Capture a frozen coded-target/view matrix in isolated Blender scenes."""
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
               'paired_truth.py', 'visibility.py', 'geometry_truth.py', 'carrier_study.py')
    provenance = {'plan_sha256': hashlib.sha256(plan_path.read_bytes()).hexdigest(),
                  'generator_sha256': {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                       for name in helpers},
                  'blender_version': bpy.app.version_string}
    (output/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    try:
        for case in plan['cases']:
            seed = case['scenario']['seed']
            scene = build_diagnostic(case)
            bpy.context.window.scene = scene
            for key in ('sv_config', 'sv_nominal_config'):
                cfg = json.loads(scene[key])
                cfg['virtual_camera'] = case['virtual_camera']
                scene[key] = json.dumps(cfg)
            (output/'progress.json').write_text(json.dumps({'seed': seed, 'state': 'capturing'}))
            capture_paired(scene, output/f'seed{seed}-capture', **case['capture'])
        (output/'progress.json').write_text(json.dumps({'state': 'complete'}))
    except Exception as error:
        (output/'progress.json').write_text(json.dumps({'state': 'failed', 'error': str(error)}))
        raise
    finally:
        bpy.context.window.scene = original
    return str(output)
