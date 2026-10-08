"""Blender 5.x compositor setup for metric camera-Z EXR ground truth."""
from pathlib import Path


def attach_depth_output(scene, directory):
    """Attach one float EXR output to the active render-layer Z pass.

    Returns the temporary node group and the old scene-node settings for restore.
    Blender 5.2 moved compositor trees from Scene.node_tree to a node-group datablock.
    """
    if not hasattr(scene, "compositing_node_group"):
        raise RuntimeError("depth truth export currently requires Blender 5.x compositor API")
    layer = scene.view_layers[0]
    Path(directory).mkdir(parents=True, exist_ok=True)
    old_use_z = layer.use_pass_z
    old_use_nodes = scene.use_nodes
    old_group = scene.compositing_node_group
    layer.use_pass_z = True
    scene.use_nodes = True
    group = __import__("bpy").data.node_groups.new("SV depth truth output", "CompositorNodeTree")
    scene.compositing_node_group = group
    render_layers = group.nodes.new("CompositorNodeRLayers")
    render_layers.scene = scene
    output = group.nodes.new("CompositorNodeOutputFile")
    output.directory = str(directory)
    output.file_name = "depth_####"
    item = output.file_output_items.new("FLOAT", "depth")
    item.format.file_format = "OPEN_EXR"
    item.format.color_depth = "32"
    group.links.new(render_layers.outputs["Depth"], output.inputs[item.name])
    return group, old_group, old_use_nodes, old_use_z, output


def restore_depth_output(scene, state):
    """Restore scene compositor settings and remove the temporary output group."""
    if state is None:
        return
    group, old_group, old_use_nodes, old_use_z, _ = state
    scene.compositing_node_group = old_group
    scene.use_nodes = old_use_nodes
    scene.view_layers[0].use_pass_z = old_use_z
    if group.users == 0:
        __import__("bpy").data.node_groups.remove(group)
