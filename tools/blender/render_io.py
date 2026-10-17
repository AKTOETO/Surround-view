"""Checked Blender still-render boundary, independently testable without bpy."""
from pathlib import Path


def render_still(scene, render):
    """Require synchronous completion and a nonempty main image at the explicit filepath."""
    expected = Path(scene.render.filepath)
    result = render(write_still=True, scene=scene.name)
    if not result or 'FINISHED' not in result:
        raise RuntimeError('Blender render did not finish: ' + str(result))
    if not expected.is_file() or expected.stat().st_size == 0:
        raise RuntimeError('Blender render produced no nonempty image: ' + str(expected))
    return expected
