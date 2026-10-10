"""Plot actual hybrid-renderer captures; no synthetic reconstruction in this figure."""
import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path,
        default=Path(__file__).parent/'figures/implementation/03_native_fusion.png')
    args = parser.parse_args()
    index = json.loads((args.capture/'index.json').read_text())
    modes = ['seam_distance_feather', 'graph_cut_seam', 'multi_band', 'graph_cut_multi_band']
    diagnostics = ['color', 'weights', 'coverage']
    fig, axes = plt.subplots(3, 4, figsize=(15, 7.5), layout='constrained')
    hashes = {}
    for column, mode in enumerate(modes):
        for row, diagnostic in enumerate(diagnostics):
            case = next(c for c in index['cases'] if c['mode'] == mode and c['diagnostic'] == diagnostic)
            path = args.capture/case['directory']/'actual.rgba'
            rgba = np.fromfile(path, np.uint8).reshape(index['height'], index['width'], 4)
            axes[row, column].imshow(rgba)
            axes[row, column].set_xticks([])
            axes[row, column].set_yticks([])
            if row == 0:
                axes[row, column].set_title(mode, fontsize=10)
            if column == 0:
                axes[row, column].set_ylabel(diagnostic)
            hashes[case['directory']] = hashlib.sha256(path.read_bytes()).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=130)
    plt.close(fig)
    args.output.with_suffix('.json').write_text(json.dumps(dict(
        kind='actual_renderer_visualization_not_quality_ranking',
        capture_index=index, rgba_sha256=hashes,
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        figure_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest()), indent=2)+'\n')


if __name__ == '__main__':
    main()
