#!/usr/bin/env python3
"""Plot external direct RGB/ROI and native samples from a completed parity test."""
import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('--run', type=Path, required=True)
parser.add_argument('--output', type=Path, default=Path(__file__).parent /
                    'figures/experiments/native_quality_parity.png')
args = parser.parse_args()
report_path = args.run / 'report.json'
report = json.loads(report_path.read_text())
check = json.loads((args.run / 'oracle-check.json').read_text())
if (not report['success'] or not report['restored'] or check['max_absolute_metric_difference'] > 1e-10
        or report['quality_reference']['kind'] != 'independent_ground_truth'):
    raise ValueError('completed direct-reference parity report required')
reference = report['quality_reference']['frames'][0]
fig, axes = plt.subplots(2, 3, figsize=(12, 5.7), layout='constrained')
axes[0, 0].imshow(Image.open(args.run / reference['rgb']))
axes[0, 0].set_title('Прямой Blender RGB, кадр 0', fontsize=11)
axes[1, 0].imshow(Image.open(args.run / reference['mask']))
axes[1, 0].set_title(f"Заданная ROI: {reference['selected_pixels']} пикселей", fontsize=11)
for index, variant in enumerate(report['scenario']['variants']):
    sample = next(s for s in report['samples'] if s['frame_index'] == 0 and s['variant'] == index)
    h, w = sample['metadata']['height'], sample['metadata']['width']
    rgba = np.frombuffer((args.run / sample['rgba_file']).read_bytes(), np.uint8).reshape(h, w, 4)
    row, column = index // 2, 1 + index % 2
    axes[row, column].imshow(rgba[..., :3])
    axes[row, column].set_title(f"{variant['mode']} / {variant['pyramid_boundary']}\n"
                               f"MAE RGB8 = {sample['quality']['mae_rgb8']:.3f}", fontsize=10)
for axis in axes.flat:
    axis.set_axis_off()
fig.suptitle('Проверка C++ оценщика на сохранённом Blender-наборе', fontsize=14)
args.output.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(args.output, dpi=160, metadata={
    'Description': 'Reused fixture metric parity; not independent holdout. Report SHA256: ' +
                   hashlib.sha256(report_path.read_bytes()).hexdigest()})
plt.close(fig)
print(args.output)
