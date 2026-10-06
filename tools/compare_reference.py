#!/usr/bin/env python3
"""Run analytic reference on identical four-camera inputs and fixed virtual view."""
import argparse
import copy
import json
from pathlib import Path

from compare_surfaces import MODES, SURFACES
from reference import run


def compare(config, manifest, output):
    base = json.loads(Path(config).read_text())
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    rows = []
    for name, surface in SURFACES.items():
        for view, elevation in (('oblique', 1.), ('low', .35)):
            for mode in MODES:
                case = copy.deepcopy(base)
                case['surface'] = surface
                case['virtual_camera'].update(azimuth_rad=.8, elevation_rad=elevation, distance_m=8.5)
                case['fusion'] = dict(mode=mode, edge_width_px=24., angle_power=2.)
                case_path = output/f'{name}-{view}-{mode}.json'
                case_path.write_text(json.dumps(case, indent=2)+'\n')
                report = run(case_path, manifest, output/f'{name}-{view}-{mode}')
                rows.append(dict(carrier=name, view=view, mode=mode, report=report))
    report = dict(schema_version=1, scope='analytic carrier validity; no physical scene visibility or quality ranking',
                  rows=rows)
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    lines = ['# Dense CPU reference: пять носителей', '',
             'Один кадр, два общих виртуальных ракурса; без треугольной дискретизации.', '',
             '| Носитель | View | Fusion | Miss pixels | ROI pixels | Valid projection % | Positive weight % |',
             '|---|---|---|---:|---:|---:|---:|']
    for row in rows:
        r = row['report']
        observed, positive = r['observed_fraction'], r['positive_weight_fraction']
        lines.append(f'| {row["carrier"]} | {row["view"]} | {row["mode"]} | {r["carrier_miss_pixels"]} | '
                     f'{r["evaluation_pixels"]} | {100*observed:.5f} | {100*positive:.5f} |')
    lines += ['', 'ROI различается между носителями. Доля покрытия не ранжирует качество.', '',
              '## Первичные отчёты', '', '```json', json.dumps(report, indent=2), '```', '']
    (output/'REPORT.md').write_text('\n'.join(lines))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    compare(args.config, args.manifest, args.output)
