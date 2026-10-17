"""Additional procedural fixture family; no Blender import or external assets."""
import numpy as np


def cargo_layout(seed):
    """A clear driving corridor with two staggered cargo rows and varied vertical depths."""
    rng = np.random.default_rng(seed)
    result = []
    for side in (-1, 1):
        for index, x in enumerate((-15., -6., 6., 15.)):
            height = float(rng.uniform(1.8, 3.6))
            result.append(dict(name=f'cargo {side} {index}',
                center=[x + float(rng.uniform(-.4, .4)), side * 4.7, height / 2],
                size=[6., 2.4, height], material=('red', 'blue', 'yellow', 'gray')[int(rng.integers(4))]))
    return result


def build_cargo_yard(scene, materials, seed, box):
    """Metric synthetic cargo yard: different topology from the street buildings/sidewalks."""
    box(scene, 'yard ground', (0, 0, -.08), (65, 38, .16), materials['pavement'])
    for y in (-2., 2.):
        for x in range(-24, 25, 4):
            box(scene, 'yard lane stripe', (x, y, .006), (2., .12, .012), materials['white'])
    layout = cargo_layout(seed)
    for item in layout:
        x, y, z = item['center']
        sx, sy, height = item['size']
        box(scene, item['name'], item['center'], item['size'], materials[item['material']])
        for offset in np.arange(-2.7, 2.8, .6):
            box(scene, 'cargo vertical rib', (x + float(offset), y - np.sign(y) * (sy / 2 + .025), z),
                (.07, .05, height), materials['metal'])
    for y in (-12., 12.):
        box(scene, 'yard boundary wall', (0, y, 1.4), (54, .25, 2.8), materials['gray'])
    for x in (-20., 20.):
        for y in (-8., 8.):
            box(scene, 'yard gantry post', (x, y, 3.5), (.3, .3, 7.), materials['yellow'])
        box(scene, 'yard gantry beam', (x, 0, 6.9), (.4, 16.4, .35), materials['yellow'])
    return layout
