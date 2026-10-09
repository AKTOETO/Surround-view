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


def run_source_capture(scene, output):
    """Small two-frame regression for camera-object shadowing and state restore."""
    import json
    from paired_truth import capture_paired
    original = scene.get('sv_config')
    cfg = json.loads(original) if original else configuration()
    for camera in cfg['cameras']:
        camera['resolution'] = {'width':16,'height':16}
        for axis in ('fx','fy'):
            camera['projection'][axis] *= .04
        for axis in ('cx','cy'):
            camera['projection'][axis] = (camera['projection'][axis]+.5)*.04-.5
    camera_object = scene.camera
    saved_matrix = np.asarray(camera_object.matrix_world).copy()
    scene['sv_config'] = json.dumps(cfg)
    try:
        capture_paired(scene, output, frames=2, face_size=32, width=16, height=16, source_ids=True)
        metadata = json.loads((Path(output)/'paired_truth.json').read_text())
        assert len(metadata['frames']) == 2
        for row in metadata['frames']:
            assert len(row['source_objects']) == 4
            for name in row['source_objects']:
                labels = np.load(Path(output)/name, allow_pickle=False)
                assert labels.shape == (16,16) and labels.dtype == np.uint16
        assert scene.camera == camera_object
        np.testing.assert_allclose(np.asarray(scene.camera.matrix_world), saved_matrix)
        return {'frames':2,'source_maps':8,'camera_state_restored':True}
    finally:
        if original is None:
            del scene['sv_config']
        else:
            scene['sv_config'] = original
