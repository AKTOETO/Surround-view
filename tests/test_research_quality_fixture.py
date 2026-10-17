"""Native evaluator parity against frozen direct Blender RGB; no algorithm implementation here."""
import argparse
from contextlib import nullcontext
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from temporal_seam_stability import load_sequence

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--dataset', type=Path)
parser.add_argument('--capture', type=Path)
parser.add_argument('--visibility-policy', choices=('ignore', 'any', 'all'), default='ignore')
parser.add_argument('--scenario', type=Path)
OPTIONS = parser.parse_args()
BUILD = OPTIONS.build.resolve()
FIXTURE = OPTIONS.dataset.resolve() if OPTIONS.dataset else ROOT / 'tests/data/paired_street_v1'
CAPTURE = OPTIONS.capture.resolve() if OPTIONS.capture else FIXTURE


def image_guard(camera, image, timestamp):
    height, width, _ = image.shape
    pixel_hash = hashlib.sha256(b'sv-packed-rgb8-v1\0' +
        struct.pack('>II', width, height) + image.tobytes()).hexdigest()
    p = camera['projection']
    values = [p[key] for key in ('fx', 'fy', 'cx', 'cy', 'theta_max_rad', 'z_epsilon_m')]
    values += p['k'] + sum(camera['T_camera_from_vehicle'], [])
    values = [0.0 if value == 0 else value for value in values]
    model_hash = hashlib.sha256(b'sv-camera-model-v1\0' +
        struct.pack('>III', camera['id'], width, height) + struct.pack('>26d', *values)).hexdigest()
    return dict(camera_id=camera['id'], calibration_id=camera['calibration_id'],
        source_timestamp_ns=timestamp, camera_model_sha256=model_hash,
        image_identity=dict(algorithm='packed_rgb8_sha256_v1', sha256=pixel_hash,
            width=width, height=height, pixel_format='RGB8', row_origin='top_left'))


class NativeQualityFixtureTests(unittest.TestCase):
    def test_direct_blender_rgb_and_numpy_metrics_match(self):
        # Existing verifier checks capture provenance, input/truth file hashes,
        # poses, ordering and the pre-existing object-ID ROI (not based on output).
        config, images, truth, rois, stamps, _ = load_sequence(FIXTURE, CAPTURE, OPTIONS.visibility_policy)
        if OPTIONS.output:
            directory = OPTIONS.output.resolve()
            directory.mkdir(parents=True, exist_ok=False)
            context = nullcontext(directory)
        else:
            context = tempfile.TemporaryDirectory(prefix='sv-native-quality-')
        with context as value:
            directory = Path(value)
            references = []
            rgb_truth = []
            for index, (frame, target, roi, stamp) in enumerate(zip(images, truth, rois, stamps)):
                rgb = np.rint(target * 255).astype(np.uint8)
                rgb_truth.append(rgb)
                rgb_name, mask_name = f'truth-{index}.ppm', f'mask-{index}.ppm'
                Image.fromarray(rgb).save(directory / rgb_name)
                Image.fromarray(np.repeat((roi * 255).astype(np.uint8)[..., None], 3, axis=2)).save(directory / mask_name)
                references.append(dict(rgb=rgb_name, mask=mask_name,
                    inputs=[image_guard(camera, image, stamp)
                            for camera, image in zip(config['cameras'], frame)]))
            view = dict(config['virtual_camera'], target_m=[0.0, 0.0, 0.0])
            reference = dict(schema_version=2, kind='independent_ground_truth',
                dataset_id='capture-' + hashlib.sha256((CAPTURE / 'capture.json').read_bytes()).hexdigest(), view=view,
                **config['output'], frames=references)
            (directory / 'reference.json').write_text(json.dumps(reference))
            scenario = dict(schema_version=1, frames=len(stamps), reset_replay=True,
                capture_frames=True, quality_reference='reference.json', warmup=0, repeats=2, seed=17,
                matrix=dict(axes=dict(mode=['multi_band', 'graph_cut_multi_band'],
                                      pyramid_boundary=['zero', 'normalized'])))
            if OPTIONS.scenario:
                scenario = json.loads(OPTIONS.scenario.read_text())
                if (scenario['frames'] != len(stamps) or not scenario['reset_replay']
                        or not scenario['capture_frames'] or scenario['quality_reference'] != 'reference.json'):
                    raise ValueError('custom fixture scenario must preserve frame count/reset/capture/reference locator')
            (directory / 'scenario.json').write_text(json.dumps(scenario))
            # Keep IPC short even when the archived report directory is deeply nested.
            with tempfile.TemporaryDirectory(prefix='sv-q-ipc-', dir='/tmp') as ipc_root:
                ipc = Path(ipc_root) / 'ipc'
                config['connections'] = dict(unix=dict(enabled=True, directory=str(ipc)))
                (directory / 'config.json').write_text(json.dumps(config))
                trace = directory / 'trace.jsonl'
                with (directory / 'server.log').open('w') as log:
                    server = subprocess.Popen([str(BUILD / 'sv-server'), '--config', str(directory / 'config.json'),
                        '--manifest', str(FIXTURE / 'manifest.json'), '--loop', 'false', '--trace', str(trace)],
                        stdout=log, stderr=log, text=True)
                    try:
                        deadline = time.monotonic() + 10
                        while not trace.exists():
                            if server.poll() is not None:
                                self.fail((directory / 'server.log').read_text())
                            if time.monotonic() > deadline:
                                self.fail('native server startup timed out')
                            time.sleep(.02)
                        report_path = directory / 'report.json'
                        run = subprocess.run([str(BUILD / 'svctl'), '--unix', str(ipc), '--timeout-ms', '10000',
                            'research', str(directory / 'scenario.json'), str(report_path)],
                            text=True, capture_output=True, timeout=70)
                        self.assertEqual(run.returncode, 0, run.stderr + run.stdout)
                        report = json.loads(report_path.read_text())
                    finally:
                        server.terminate()
                        try:
                            server.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            server.kill()
                            server.wait(timeout=5)
                            raise
                    self.assertEqual(server.returncode, 0, (directory / 'server.log').read_text())
            self.assertTrue(report['success'])
            self.assertTrue(report['restored'])
            self.assertEqual(report['quality_reference']['kind'], 'independent_ground_truth')
            self.assertEqual(report['quality_reference']['schema_version'], 2)
            self.assertEqual(report['quality_acceptance']['status'], 'not_requested')
            self.assertEqual(len(report['samples']), len(stamps) * len(report['scenario']['variants']) *
                             (scenario['warmup'] + scenario['repeats']))
            maximum_difference = 0.0
            values = []
            for sample in report['samples']:
                if sample['warmup']:
                    self.assertNotIn('quality', sample)
                    continue
                index = sample['frame_index']
                raw = np.frombuffer((directory / sample['rgba_file']).read_bytes(), np.uint8)
                actual = raw.reshape(config['output']['height'], config['output']['width'], 4)[..., :3]
                delta = actual.astype(np.int32) - rgb_truth[index].astype(np.int32)
                selected = delta[rois[index]]
                mse = float(np.square(selected).sum()) / selected.size
                expected = dict(mae_rgb8=float(np.abs(selected).sum()) / selected.size,
                    rmse_rgb8=float(np.sqrt(mse)), psnr_db=None if mse == 0 else float(10 * np.log10(255.0**2 / mse)),
                    max_absolute_rgb8=int(np.abs(selected).max()))
                quality = sample['quality']
                self.assertEqual(quality['selected_pixels'], int(rois[index].sum()))
                self.assertEqual(quality['exact_match'], mse == 0)
                for key, number in expected.items():
                    if number is None:
                        self.assertIsNone(quality[key])
                        continue
                    maximum_difference = max(maximum_difference, abs(quality[key] - number))
                    self.assertAlmostEqual(quality[key], number, delta=1e-10, msg=key)
                values.append(dict(frame_index=index, variant=sample['variant'],
                    block=sample['block'], **expected))
            for summary in report['quality_summaries']:
                self.assertEqual(summary['frames'], len(stamps))
                self.assertEqual(summary['selected_pixels'], sum(int(roi.sum()) for roi in rois))
            result = dict(schema_version=1, purpose='native metric integration parity; not confirmatory holdout',
                comparisons=len(values) * 4, samples=len(values), visibility_policy=OPTIONS.visibility_policy, max_absolute_metric_difference=maximum_difference,
                selected_pixels=[int(roi.sum()) for roi in rois], values=values)
            (directory / 'oracle-check.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps({key: value for key, value in result.items() if key != 'values'}))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
