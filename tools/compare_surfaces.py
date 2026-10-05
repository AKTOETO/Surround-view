#!/usr/bin/env python3
"""Screen surface/fusion variants on identical recorded inputs; not a quality ranking."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image


MODES = ('edge_feather', 'hard_best_angle', 'angular_feather')
SURFACES = {
    'dome':dict(type='dome_floor_v1', dome_radius_m=12., dome_latitude_cells=64,
                dome_longitude_cells=128, floor_radial_cells=32),
    'cylinder':dict(type='cylinder_floor_v1', radius_m=12., height_m=12., vertical_cells=32,
                    angular_cells=128, floor_radial_cells=32),
    'cube':dict(type='cube_floor_v1', half_extent_m=12., height_m=12., face_cells=32),
    'plane':dict(type='rectangular_bowl_v1', flat_half_length_m=2.6, flat_half_width_m=1.2,
                 outer_half_length_m=12., outer_half_width_m=12., corner_height_m=0.,
                 uniform_cells=[64,64]),
    'bowl':dict(type='rectangular_bowl_v1', flat_half_length_m=2.6, flat_half_width_m=1.2,
                outer_half_length_m=12., outer_half_width_m=12., corner_height_m=1.5,
                uniform_cells=[64,64]),
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def coverage_counts(path):
    with Image.open(path) as image:
        rgb = np.array(image.convert('RGB'),dtype=int)
    known = np.zeros(rgb.shape[:2],bool)
    counts = {}
    for cameras in range(5):
        mask = np.max(np.abs(rgb - round(cameras*255/4)),axis=-1) <= 1
        counts[str(cameras)] = int(mask.sum())
        known |= mask
    vehicle = np.all(rgb == [255,0,255],axis=-1)
    counts['vehicle_mask'] = int(vehicle.sum())
    counts['outside_surface'] = int((~known & ~vehicle).sum())
    counts['total_pixels'] = int(rgb.shape[0]*rgb.shape[1])
    return counts


def compare(binary, config, manifest, output, carriers, iterations, warmup, backend):
    binary, config, manifest, output = (Path(p).resolve() for p in (binary,config,manifest,output))
    base = json.loads(config.read_text())
    recording = json.loads(manifest.read_text())
    if not recording.get('sha256') or not recording.get('frames'):
        raise ValueError('a hashed recording with frames is required')
    if any(path not in recording['sha256'] for path in recording['frames'][0]['paths']):
        raise ValueError('all four first-frame inputs must have checksums')
    for name, expected in recording['sha256'].items():
        path = (manifest.parent/name).resolve()
        if not path.is_relative_to(manifest.parent) or digest(path) != expected:
            raise ValueError(f'unsafe recording path or checksum mismatch: {name}')
    output.mkdir(parents=True,exist_ok=False)
    rows = []
    for carrier in carriers:
        for view, elevation in (('oblique',1.),('low',.35)):
            for diagnostic in ('color','weights','coverage'):
                for mode in MODES if diagnostic != 'coverage' else MODES[:1]:
                    name = f'{carrier}-{view}-{mode}-{diagnostic}'
                    case = copy.deepcopy(base)
                    case['surface'] = SURFACES[carrier]
                    case['virtual_camera'].update(azimuth_rad=.8,elevation_rad=elevation,distance_m=8.5)
                    case['fusion'] = dict(mode=mode,diagnostic=diagnostic,edge_width_px=24.,angle_power=2.)
                    filename = output/f'{name}.json'
                    filename.write_text(json.dumps(case,indent=2)+'\n')
                    destination = output/name
                    result = subprocess.run([str(binary),'--config',str(filename),'--manifest',str(manifest),
                        '--output',str(destination),'--egl-platform',backend,'--iterations',str(iterations),
                        '--warmup',str(warmup)],capture_output=True,text=True,timeout=120)
                    if result.returncode:
                        raise RuntimeError(f'{name}: {result.stderr}')
                    metrics = json.loads((destination/'metrics.json').read_text())
                    with Image.open(destination/'preview.ppm') as image:
                        image.save(destination/'preview.png')
                    row = dict(carrier=carrier,view=view,mode=mode,diagnostic=diagnostic,
                               config=name+'.json',directory=name,metrics=metrics,
                               preview_sha256=digest(destination/'preview.png'))
                    if diagnostic == 'coverage':
                        row['coverage_counts'] = coverage_counts(destination/'preview.png')
                    rows.append(row)
                    print(name,flush=True)
    report = dict(schema_version=1,scope='first-frame screening; unequal mesh budgets; no seam/ghosting ranking',
                  config_sha256=digest(config),manifest_sha256=digest(manifest),binary_sha256=digest(binary),
                  script_sha256=digest(__file__),iterations=iterations,warmup=warmup,
                  frames_used=1, rows=rows)
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    lines = ['# Screening носителей и слияния четырёх камер','',
             'Один первый набор записи; кадры, calibration и виртуальные ракурсы одинаковы. ',
             'Плотность/число треугольников различаются. Это подготовительный опыт, не ранжирование качества.',
             '',f'Manifest SHA-256: `{report["manifest_sha256"]}`','',
             '| Surface | View | Fusion | Triangles | GL renderer | p95 smoke ms |',
             '|---|---|---|---:|---|---:|']
    for row in rows:
        if row['diagnostic'] == 'color':
            m = row['metrics']
            lines.append(f'| {row["carrier"]} | {row["view"]} | {row["mode"]} | {m["triangles"]} | '
                         f'{m["gl_renderer"]} | {m["p95_ms"]:.4f} |')
    lines += ['','## Диагностическое покрытие','',
              '| Surface | View | 0 cams | 1 cam | 2 cams | 3 cams | 4 cams | Mask | Outside surface |',
              '|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for row in rows:
        if row['diagnostic'] == 'coverage':
            counts = row['coverage_counts']
            values = [counts[str(i)] for i in range(5)]+[counts['vehicle_mask'],counts['outside_surface']]
            lines.append('| '+row['carrier']+' | '+row['view']+' | '+' | '.join(map(str,values))+' |')
    lines += ['','0 cams — поверхность без наблюдения; outside surface — экран за геометрией.',
              'Mask исключается из статистики. Coverage проверяет validity, независимо от положительности весов.',
              'Graph-cut, multi-band, depth/visibility truth и video seam stability в этом опыте не оцениваются.','']
    (output/'REPORT.md').write_text('\n'.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=Path('build/sv-bench'))
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--carriers',nargs='+',choices=list(SURFACES),default=list(SURFACES))
    parser.add_argument('--iterations',type=int,default=3)
    parser.add_argument('--warmup',type=int,default=1)
    parser.add_argument('--egl-platform',choices=['default','surfaceless','device'],default='surfaceless')
    args = parser.parse_args()
    if not 1 <= args.iterations <= 10000 or not 0 <= args.warmup <= 1000:
        parser.error('iterations=1..10000, warmup=0..1000 required')
    compare(args.binary,args.config,args.manifest,args.output,args.carriers,args.iterations,args.warmup,args.egl_platform)
