"""All frozen native products must remain byte-identical after the boundary fix.

Requires retained research inputs; intentionally excluded from ordinary CTest.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/research'))
from source_correspondence import sha, verified
from native_uncertainty import checked_plan


def verify(plan_path, binary):
    plan = json.loads(plan_path.read_text())
    parent_path = verified(ROOT / plan['parent_plan'], plan['parent_plan_sha256'])
    parent = checked_plan(ROOT, parent_path)
    primary = json.loads(verified(ROOT / plan['primary_report'], plan['primary_report_sha256']).read_text())
    verified(binary, plan['binary_sha256'])
    verified(ROOT / 'src/render/surface.frag', plan['shader_sha256'])
    output = ROOT / plan['output_root']
    if output.exists():
        raise ValueError('use a fresh parity directory; preserve existing results')
    output.mkdir(parents=True)
    copied = output / 'sv-correspondence-probe'
    shutil.copy2(binary, copied)
    import os
    environment = dict(os.environ)
    environment.update(parent['backend_environment'])
    results = []
    for condition, row in zip(parent['conditions'], primary['rows'], strict=True):
        if condition['frame'] != row['frame'] or condition['variant'] != row['variant']:
            raise ValueError('reference selection differs')
        name = f"frame{condition['frame']}-{condition['variant']}"
        reference_dir = ROOT / row['directory']
        reference = json.loads(verified(reference_dir / 'report.json', row['report_sha256']).read_text())
        config = ROOT / parent['config_root'] / (name + '.json')
        with tempfile.TemporaryDirectory(prefix='sv-uncertainty-parity-') as temporary:
            actual_dir = Path(temporary)
            command = [str(copied), str(config), str(ROOT / parent['manifest']), temporary, str(condition['frame'])]
            subprocess.run(command, env=environment, check=True, capture_output=True)
            actual = json.loads((actual_dir / 'report.json').read_text())
            if (actual['source_revision'] != plan['source_revision']
                    or actual['source_fingerprint'] != plan['source_fingerprint']
                    or parent['required_device_substring'] not in actual['device']
                    or actual['config_sha256'] != reference['config_sha256']
                    or actual['inputs'] != reference['inputs'] or not actual['normal_output_unchanged']):
                raise ValueError('new capture provenance differs')
            if actual['products'] != reference['products']:
                raise ValueError('native product descriptors/hashes differ: ' + name)
            for product in actual['products']:
                verified(actual_dir / product['file'], product['sha256'])
                verified(reference_dir / product['file'], product['sha256'])
            retained = output / name
            retained.mkdir()
            shutil.copyfile(actual_dir / 'output.rgba', retained / 'output.rgba')
            shutil.copyfile(actual_dir / 'report.json', retained / 'capture-metadata.json')
            results.append(dict(frame=condition['frame'], variant=condition['variant'],
                reference_report_sha256=row['report_sha256'], new_report_sha256=sha(retained / 'capture-metadata.json'),
                output_sha256=sha(retained / 'output.rgba'), exact_product_count=len(actual['products'])))
            print(name, flush=True)
    return dict(schema_version=1, scope=plan['scope'], plan_sha256=sha(plan_path),
        runner_sha256=sha(__file__), binary_sha256=sha(copied), rows=results, limitations=plan['limitations'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, default=ROOT / 'configs/research/native-uncertainty-degenerate-parity-plan.json')
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/sv-correspondence-probe')
    parser.add_argument('--output-report', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.plan, args.binary)
    args.output_report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
