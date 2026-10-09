"""Blender-only regression: hide_render helper cannot occlude independent truth.

Run inside active Blender via runpy.run_path(...)["run"](bpy.context.scene).
Creates and removes only its own temporary mesh; no RGB render or user-scene edits.
"""
from pathlib import Path
import sys

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools/blender'))
from paired_truth import object_truth, virtual_pose
from rig import configuration


def run(scene):
    cfg = configuration()
    cfg['output'] = {'width': 16, 'height': 16}
    vehicle = np.asarray(scene.objects[scene['sv_ego']].matrix_world)
    pose = vehicle @ virtual_pose(cfg)
    ids = {obj.name: i+1 for i, obj in enumerate(scene.objects) if obj.type == 'MESH'}
    original_labels, original_visibility = object_truth(scene, pose, cfg, ids, vehicle)
    mesh = bpy.data.meshes.new('SV visibility test disposable mesh')
    helper = bpy.data.objects.new('SV visibility test disposable helper', mesh)
    try:
        scene.collection.objects.link(helper)
        corners = [(pose @ [x, y, -.3, 1])[:3] for x, y in ((-1,-1), (1,-1), (1,1), (-1,1))]
        mesh.from_pydata(corners, [], [(0, 1, 2, 3)])
        ids[helper.name] = 65000
        helper.hide_render = True
        bpy.context.view_layer.update()
        hidden_labels, hidden_visibility = object_truth(scene, pose, cfg, ids, vehicle)
        np.testing.assert_array_equal(hidden_labels, original_labels)
        np.testing.assert_array_equal(hidden_visibility, original_visibility)
        helper.hide_render = False
        bpy.context.view_layer.update()
        visible_labels, _ = object_truth(scene, pose, cfg, ids, vehicle)
        count = int(np.count_nonzero(visible_labels == 65000))
        assert count == 256, f'visible foreground helper should cover every pixel, got {count}'
        return {'hidden_helper_preserves_labels': True,
                'hidden_helper_preserves_visibility': True, 'visible_occluder_pixels': count}
    finally:
        bpy.data.objects.remove(helper, do_unlink=True)
        bpy.data.meshes.remove(mesh)
        bpy.context.view_layer.update()
