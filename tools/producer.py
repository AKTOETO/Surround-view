#!/usr/bin/env python3
"""Stream a verified replay dataset to four explicitly configured virtual cameras."""
import argparse
import hashlib
import json
import socket
import threading
import time
from pathlib import Path

from PIL import Image
from ipc import pack, receive


def stream(config, manifest_path, loops=1, host=None, stop=None):
    stop = stop or threading.Event()
    manifest_path = Path(manifest_path).resolve()
    manifest = json.loads(manifest_path.read_text())
    cameras = sorted(config['cameras'], key=lambda c: c['id'])
    endpoints = sorted(config['source']['cameras'], key=lambda e: e['camera_id'])
    if config['source']['type'] != 'socket' or len(cameras) != 4 or len(endpoints) != 4:
        raise ValueError('four socket cameras required')
    if manifest['schema_version'] != 1 or manifest['calibration_ids'] != [c['calibration_id'] for c in cameras]:
        raise ValueError('manifest calibration mismatch')
    rows = manifest['frames']
    if not rows or loops < 1:
        raise ValueError('nonempty manifest and positive loops required')
    times = [int(row['scenario_timestamp_ns']) for row in rows]
    if any(t < 0 or t >= 2**64 for t in times) or any(a >= b for a, b in zip(times, times[1:])):
        raise ValueError('invalid scenario timeline')
    paths = {}
    for row in rows:
        if row.get('offset_ns') != [0, 0, 0, 0]:
            raise ValueError('producer does not simulate replay timestamp offsets')
        if len(row['paths']) != 4:
            raise ValueError('four paths required')
        for name in row['paths']:
            if name is None or name == '':
                continue
            path = (manifest_path.parent / name).resolve()
            if not path.is_relative_to(manifest_path.parent):
                raise ValueError('dataset path escapes directory')
            if name not in paths:
                if hashlib.sha256(path.read_bytes()).hexdigest() != manifest['sha256'][name]:
                    raise ValueError('dataset checksum mismatch: ' + name)
                paths[name] = path
    errors = []
    epoch = time.monotonic_ns() + 200_000_000
    period = times[-1] - times[0] + (times[1] - times[0] if len(times) > 1 else 33_333_333)

    def camera_worker(camera, endpoint):
        connection = None
        sequence = 0
        cached_name, pixels = None, None
        try:
            for loop in range(loops):
                for row, timestamp in zip(rows, times):
                    if stop.is_set():
                        return
                    name = row['paths'][camera['id']]
                    if not name:
                        continue
                    resolution = camera['resolution']
                    if name != cached_name:
                        with Image.open(paths[name]) as image:
                            if image.size != (resolution['width'], resolution['height']):
                                raise ValueError('input resolution differs from calibration')
                            pixels = image.convert('RGB').tobytes()
                        cached_name = name
                    target = epoch + loop * period + timestamp - times[0]
                    if stop.wait(max(0, (target - time.monotonic_ns()) / 1e9)):
                        return
                    if connection is None:
                        if endpoint['transport'] == 'unix':
                            connection = socket.socket(socket.AF_UNIX)
                            connection.settimeout(3)
                            connection.connect(endpoint['path'])
                        else:
                            connection = socket.create_connection((host or endpoint['address'], endpoint['port']), timeout=3)
                        connection.sendall(pack(1, dict(role='producer', camera_id=camera['id'],
                            calibration_id=camera['calibration_id'], **resolution,
                            pixel_format='RGB8', row_origin='top_left', clock_domain='producer_monotonic')))
                        kind, hello, payload = receive(connection)
                        if kind != 2 or payload or hello['camera_id'] != camera['id']:
                            raise ValueError('producer handshake rejected')
                    header = dict(session_id=hello['session_id'], camera_id=camera['id'],
                        calibration_id=camera['calibration_id'], **resolution,
                        stride_bytes=resolution['width'] * 3, pixel_format='RGB8', row_origin='top_left',
                        clock_domain='producer_monotonic', sequence_id=str(sequence),
                        source_timestamp_ns=str(time.monotonic_ns()), scenario_timestamp_ns=str(timestamp))
                    connection.sendall(pack(10, header, pixels))
                    sequence += 1
        except Exception as error:
            errors.append((camera['id'], str(error)))
        finally:
            if connection is not None:
                connection.close()

    workers = [threading.Thread(target=camera_worker, args=pair) for pair in zip(cameras, endpoints)]
    for worker in workers:
        worker.start()
    try:
        for worker in workers:
            worker.join()
    except KeyboardInterrupt:
        stop.set()
        for worker in workers:
            worker.join()
        raise
    if errors:
        raise RuntimeError(f'camera producers failed: {errors}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--loops', type=int, default=1)
    parser.add_argument('--host', help='destination address override for TCP endpoints only')
    args = parser.parse_args()
    stream(json.loads(args.config.read_text()), args.manifest, args.loops, args.host)


if __name__ == '__main__':
    main()
