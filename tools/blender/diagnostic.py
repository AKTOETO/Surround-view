"""An isolated coded target makes RGB object identity observable in a controlled study."""
import json
import bpy
from scene import box, build_scene
from diagnostic_motion import frame_positions


def build_diagnostic(plan):
    scene = build_scene(plan['scenario'])
    material = bpy.data.materials.new('SV diagnostic magenta emission')
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    emission = nodes.new('ShaderNodeEmission')
    emission.inputs['Color'].default_value = (*plan['target']['linear_rgb'], 1.)
    emission.inputs['Strength'].default_value = 1.
    output = nodes.new('ShaderNodeOutputMaterial')
    material.node_tree.links.new(emission.outputs[0], output.inputs['Surface'])
    target = box(scene, 'coded object target', plan['target']['center_m'],
                 plan['target']['size_m'], material)
    keyed_positions = frame_positions(plan['target'], plan['capture'])
    if keyed_positions:
        scene.frame_set(keyed_positions[0][0])
        bpy.context.view_layer.update()
    scene['sv_diagnostic_target'] = json.dumps({'object_name':target.name, **plan['target']})
    return scene
