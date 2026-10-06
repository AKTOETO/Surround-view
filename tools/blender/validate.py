#!/usr/bin/env python3
"""Smoke-test an exported dataset through sv-bench and sv-server's Unix channels."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ipc import Client


def validate(build, dataset, output, source='replay'):
    build, dataset, output = (Path(p).resolve() for p in (build,dataset,output))
    output.mkdir(parents=True,exist_ok=False)
    manifest = json.loads((dataset/'manifest.json').read_text())
    config = json.loads((dataset/'config.json').read_text())
    if len(manifest['frames']) < 2:
        raise ValueError('at least two frames required for motion validation')
    for name,digest in manifest['sha256'].items():
        path = (dataset/name).resolve()
        if not path.is_relative_to(dataset) or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'invalid file checksum: {name}')
    for camera in range(4):
        hashes = {manifest['sha256'][row['paths'][camera]] for row in manifest['frames']}
        if len(hashes) < 2:
            raise ValueError(f'camera {camera} does not change during motion')
    env = dict(os.environ, EGL_PLATFORM='surfaceless', SV_EGL_PLATFORM='surfaceless')
    inputs = ['--config',str(dataset/'config.json'),'--manifest',str(dataset/'manifest.json')]
    subprocess.run([str(build/'sv-bench'),*inputs,'--output',str(output/'bench'),
                    '--egl-platform','surfaceless','--iterations','2','--warmup','1'],
                   env=env,check=True,timeout=60,capture_output=True,text=True)
    with tempfile.TemporaryDirectory(prefix='sv-blender-ipc-') as temporary:
        ipc = Path(temporary)/'ipc'
        server_inputs = [*inputs, '--ipc-dir', str(ipc)]
        if source == 'socket':
            config['connections'] = {'unix': {'enabled': True, 'directory': str(ipc)}}
            config['source'] = {'type': 'socket', 'cameras': [
                {'camera_id': i, 'transport': 'unix', 'path': str(Path(temporary)/f'camera{i}.sock')}
                for i in range(4)]}
            deployment = output/'socket-config.json'
            deployment.write_text(json.dumps(config, indent=2)+'\n')
            server_inputs = ['--config', str(deployment)]
        server = subprocess.Popen([str(build/'sv-server'),*server_inputs,
                                   '--trace',str(output/'server.jsonl')],
                                  env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        client = None
        producer = None
        try:
            deadline = time.monotonic()+10
            while not (ipc/'data.sock').exists():
                if server.poll() is not None:
                    raise RuntimeError(server.communicate()[1])
                if time.monotonic() > deadline:
                    raise TimeoutError('server startup')
                time.sleep(.02)
            client = Client(ipc)
            if source == 'socket':
                producer = subprocess.Popen([sys.executable, str(Path(__file__).resolve().parents[1]/'producer.py'),
                    '--config', str(deployment), '--manifest', str(dataset/'manifest.json'), '--loops', '200'],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            frames = []
            while len(frames) < 4:
                header,pixels = client.frame()
                image = np.frombuffer(pixels,np.uint8).reshape(header['height'],header['width'],4)
                if source == 'socket' and header['health'] != 'READY':
                    if producer.poll() is not None:
                        raise RuntimeError('producer exited: '+producer.communicate()[1])
                    if time.monotonic() > deadline:
                        raise TimeoutError('four-camera socket READY')
                    continue
                if source == 'socket' and (header.get('source_type') != 'socket' or any(
                        camera.get('source_clock_domain') != 'producer_monotonic' for camera in header['inputs'])):
                    raise RuntimeError('missing source provenance')
                if header['health'] != 'READY' or np.any(image[:,:,3] != 255):
                    raise RuntimeError('non-ready frame or transparent holes')
                if (header['width'],header['height']) != (config['output']['width'],config['output']['height']):
                    raise RuntimeError('output resolution mismatch')
                if not frames:
                    from PIL import Image
                    Image.fromarray(image).save(output/'server-view.png')
                frames.append(header['frame_id'])
            if len(set(frames)) != 4:
                raise RuntimeError('duplicate server frame IDs')
            ack = client.command('preset',name='front')
            if not ack['accepted']:
                raise RuntimeError('view command rejected')
            while True:
                header,_ = client.frame()
                if int(header['state_revision']) >= int(ack['state_revision']):
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError('view command was not applied')
        finally:
            if producer:
                producer.terminate()
                producer.communicate(timeout=5)
            if client:
                client.close()
            server.terminate()
            _,stderr = server.communicate(timeout=5)
            if server.returncode not in (0,-15):
                raise RuntimeError(stderr)
    metrics = json.loads((output/'bench'/'metrics.json').read_text())
    report = {'status':'PASS','source_mode':source,'input_frames':len(manifest['frames']),
              'input_files_checked':len(manifest['sha256']), 'server_frames_checked':4,
              'all_cameras_change':True,'rgba_opaque':True,'view_command_applied':True,
              'manifest_sha256':hashlib.sha256((dataset/'manifest.json').read_bytes()).hexdigest(),
              'gl_vendor':metrics['gl_vendor'],'gl_renderer':metrics['gl_renderer'],
              'scope':'Linux smoke; not performance, seamless fusion or target-platform acceptance'}
    (output/'smoke.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=Path('build'))
    parser.add_argument('--dataset',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--source',choices=['replay','socket'],default='replay')
    args = parser.parse_args()
    validate(args.build,args.dataset,args.output,args.source)
