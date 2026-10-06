#!/usr/bin/env python3
"""Stream verified camera recordings with bounded reconnect and no application backlog."""
import argparse
from datetime import datetime, timezone
import hashlib
import ipaddress
import json
import os
import select
import signal
import socket
import sys
import threading
import time
from pathlib import Path

from PIL import Image, __version__ as pillow_version
from ipc import pack, receive


class ProducerError(RuntimeError):
    def __init__(self, report):
        self.report = report
        failures = [(c['camera_id'], c['last_error']) for c in report['cameras'] if c['status'] == 'failed']
        super().__init__(f'camera producers failed: {failures}')


def _dataset(config, manifest_path, loops, host):
    manifest_path = Path(manifest_path).resolve()
    manifest = json.loads(manifest_path.read_text())
    cameras = sorted(config['cameras'], key=lambda c: c['id'])
    endpoints = sorted(config['source']['cameras'], key=lambda e: e['camera_id'])
    if (config['source']['type'] != 'socket' or [c['id'] for c in cameras] != list(range(4)) or
            [e['camera_id'] for e in endpoints] != list(range(4))):
        raise ValueError('four distinct socket cameras required')
    for camera, endpoint in zip(cameras, endpoints):
        resolution = camera['resolution']
        if (not 2 <= resolution['width'] <= 4096 or not 2 <= resolution['height'] <= 2160 or
                not camera['calibration_id']):
            raise ValueError('invalid camera resolution/calibration')
        if endpoint['transport'] == 'tcp':
            ipaddress.ip_address(host or endpoint['address'])  # Avoid unbounded DNS resolution.
            if not 1 <= endpoint['port'] <= 65535:
                raise ValueError('invalid camera port')
        elif endpoint['transport'] != 'unix' or not Path(endpoint['path']).is_absolute():
            raise ValueError('Unix/TCP camera endpoint required')
    if manifest['schema_version'] != 1 or manifest['calibration_ids'] != [c['calibration_id'] for c in cameras]:
        raise ValueError('manifest calibration mismatch')
    rows = manifest['frames']
    if not 1 <= len(rows) <= 100000 or loops < 1:
        raise ValueError('nonempty bounded manifest and positive loops required')
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
                digest = hashlib.sha256()
                with path.open('rb') as source:
                    for block in iter(lambda: source.read(65536), b''):
                        digest.update(block)
                if digest.hexdigest() != manifest['sha256'][name]:
                    raise ValueError('dataset checksum mismatch: ' + name)
                paths[name] = path
    return manifest_path, cameras, endpoints, rows, times, paths


def _receive_hello(connection, timeout_ms):
    deadline = time.monotonic() + timeout_ms / 1000.

    class DeadlineReader:
        def recv(self, count):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('producer handshake deadline expired')
            connection.settimeout(remaining)
            return connection.recv(count)

    try:
        return receive(DeadlineReader())
    finally:
        connection.settimeout(timeout_ms / 1000.)


def stream(config, manifest_path, loops=1, host=None, stop=None, *, reconnect_attempts=5,
           reconnect_delay_ms=100, timeout_ms=1000, max_lateness_ms=100, report_path=None):
    if (not 0 <= reconnect_attempts <= 10000 or not 1 <= reconnect_delay_ms <= 60000 or
            not 10 <= timeout_ms <= 60000 or not 0 <= max_lateness_ms <= 60000):
        raise ValueError('invalid reconnect/timeout/lateness limits')
    if report_path is not None:
        destination = Path(report_path)
        if destination.suffix != '.json':
            raise ValueError('report must use .json suffix')
        if destination.exists() or destination.with_suffix('.md').exists():
            raise ValueError('report already exists')
    manifest_path, cameras, endpoints, rows, times, paths = _dataset(config, manifest_path, loops, host)
    stop = stop or threading.Event()
    started = time.monotonic_ns()
    implementation_files = {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                            for name in ('producer.py', 'ipc.py')}
    implementation_hash = hashlib.sha256(json.dumps(implementation_files, sort_keys=True).encode()).hexdigest()
    report = dict(schema_version=1, suite_id='surround-view-producer-v1', status='running',
        started_utc=datetime.now(timezone.utc).isoformat(), clock_domain='producer_monotonic',
        implementation_sha256=implementation_hash, implementation_files=implementation_files,
        python_version=sys.version.split()[0], pillow_version=pillow_version,
        manifest_sha256=hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        config_sha256=hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest(),
        settings=dict(loops=loops, reconnect_attempts=reconnect_attempts, reconnect_delay_ms=reconnect_delay_ms,
            timeout_ms=timeout_ms, max_lateness_ms=max_lateness_ms), cameras=[])
    for camera in cameras:
        report['cameras'].append(dict(camera_id=camera['id'], status='running',
            scheduled_frames=len(rows)*loops, sent_frames=0, skipped_late_frames=0,
            skipped_missing_frames=0, connect_attempts=0, connections=0, reconnects=0,
            transport_failures=0, payload_bytes=0, max_lateness_ms=0., max_send_block_ms=0.,
            last_session_id=None, last_error=None))
    epoch = started + 200_000_000
    period = times[-1] - times[0] + (times[1] - times[0] if len(times) > 1 else 33_333_333)

    def wait_until(target):
        while not stop.is_set():
            remaining = (target - time.monotonic_ns()) / 1e9
            if remaining <= 0:
                return True
            stop.wait(min(remaining, 1.))
        return False

    def camera_worker(camera, endpoint):
        stats = report['cameras'][camera['id']]
        connection = None
        sequence, failures = 0, 0
        cached_name, pixels = None, None
        resolution = camera['resolution']

        def close():
            nonlocal connection
            if connection is not None:
                connection.close()
                connection = None

        def late(target):
            elapsed = max(0, (time.monotonic_ns() - target) / 1e6)
            stats['max_lateness_ms'] = max(stats['max_lateness_ms'], elapsed)
            return elapsed > max_lateness_ms

        try:
            for loop in range(loops):
                for row, timestamp in zip(rows, times):
                    if stop.is_set():
                        return
                    name = row['paths'][camera['id']]
                    if not name:
                        stats['skipped_missing_frames'] += 1
                        continue
                    target = epoch + loop * period + timestamp - times[0]
                    if late(target):
                        stats['skipped_late_frames'] += 1
                        continue
                    if name != cached_name:
                        with Image.open(paths[name]) as image:
                            if image.size != (resolution['width'], resolution['height']):
                                raise ValueError('input resolution differs from calibration')
                            pixels = image.convert('RGB').tobytes()
                        cached_name = name
                    if not wait_until(target):
                        return
                    while not stop.is_set():
                        if late(target):
                            stats['skipped_late_frames'] += 1
                            break
                        try:
                            if connection is None:
                                stats['connect_attempts'] += 1
                                if endpoint['transport'] == 'unix':
                                    connection = socket.socket(socket.AF_UNIX)
                                    address = endpoint['path']
                                else:
                                    address_ip = ipaddress.ip_address(host or endpoint['address'])
                                    connection = socket.socket(socket.AF_INET6 if address_ip.version == 6 else socket.AF_INET)
                                    address = (str(address_ip), endpoint['port'])
                                connection.settimeout(timeout_ms / 1000.)
                                connection.connect(address)
                                if stop.is_set():
                                    return
                                connection.sendall(pack(1, dict(role='producer', camera_id=camera['id'],
                                    calibration_id=camera['calibration_id'], **resolution,
                                    pixel_format='RGB8', row_origin='top_left', clock_domain='producer_monotonic')))
                                if stop.is_set():
                                    return
                                kind, hello, payload = _receive_hello(connection, timeout_ms)
                                if (kind != 2 or payload or hello['camera_id'] != camera['id'] or
                                        not isinstance(hello['session_id'], str) or not hello['session_id'] or
                                        hello['timestamp_basis'] != 'server_delivery'):
                                    raise ValueError('producer handshake rejected')
                                stats['connections'] += 1
                                stats['reconnects'] = stats['connections'] - 1
                                stats['last_session_id'] = hello['session_id']
                                sequence = 0
                                continue  # Recheck lateness after connect/handshake.
                            if select.select([connection], [], [], 0)[0]:
                                if connection.recv(1, socket.MSG_PEEK) == b'':
                                    raise EOFError('camera channel closed')
                                raise ValueError('unexpected message after producer handshake')
                            header = dict(session_id=stats['last_session_id'], camera_id=camera['id'],
                                calibration_id=camera['calibration_id'], **resolution,
                                stride_bytes=resolution['width'] * 3, pixel_format='RGB8', row_origin='top_left',
                                clock_domain='producer_monotonic', sequence_id=str(sequence),
                                source_timestamp_ns=str(time.monotonic_ns()), scenario_timestamp_ns=str(timestamp))
                            message = pack(10, header, pixels)
                            before = time.monotonic_ns()
                            try:
                                connection.sendall(message)
                            finally:
                                stats['max_send_block_ms'] = max(stats['max_send_block_ms'],
                                    (time.monotonic_ns() - before) / 1e6)
                            stats['sent_frames'] += 1
                            stats['payload_bytes'] += len(pixels)
                            sequence += 1
                            failures = 0  # Budget is per consecutive outage, not per recording.
                            break
                        except (OSError, EOFError) as error:
                            stats['transport_failures'] += 1
                            stats['last_error'] = str(error)[:1024]
                            close()
                            failures += 1
                            if stop.is_set():
                                return
                            if failures > reconnect_attempts:
                                raise RuntimeError('reconnect budget exhausted: ' + str(error)) from error
                            if stop.wait(reconnect_delay_ms / 1000.):
                                return
            if stop.is_set():
                return
            if not stats['sent_frames'] and stats['skipped_missing_frames'] < stats['scheduled_frames']:
                raise RuntimeError('no frames sent before the recording ended')
            stats['status'] = 'completed'
        except Exception as error:
            stats['status'] = 'failed'
            stats['last_error'] = str(error)[:1024]
        finally:
            close()
            if stats['status'] == 'running':
                stats['status'] = 'cancelled'

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
    report['duration_ms'] = (time.monotonic_ns() - started) / 1e6
    report['status'] = ('failed' if any(c['status'] == 'failed' for c in report['cameras']) else
                        'cancelled' if stop.is_set() else 'completed')
    if report_path is not None:
        destination = Path(report_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with os.fdopen(os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644), 'w') as output:
            json.dump(report, output, indent=2, allow_nan=False)
            output.write('\n')
        lines = ['# Отчёт producer камер', '', f"Состояние: **{report['status']}**.",
            '', 'Sent означает завершённый sendall; ACK обработки сервером отсутствует.',
            'Времена относятся к producer monotonic clock; это не capture-to-display latency.', '',
            '| Камера | Статус | Sent | Late skip | Missing | Reconnects | Transport errors | Max send, ms |',
            '|---|---|---:|---:|---:|---:|---:|---:|']
        for camera in report['cameras']:
            lines.append(f"| {camera['camera_id']} | {camera['status']} | {camera['sent_frames']} | "
                f"{camera['skipped_late_frames']} | {camera['skipped_missing_frames']} | "
                f"{camera['reconnects']} | {camera['transport_failures']} | {camera['max_send_block_ms']:.3f} |")
        lines.extend(['', '```json', json.dumps(report, indent=2, allow_nan=False), '```', ''])
        with os.fdopen(os.open(destination.with_suffix('.md'), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644), 'w') as output:
            output.write('\n'.join(lines))
    if report['status'] == 'failed':
        raise ProducerError(report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--loops', type=int, default=1)
    parser.add_argument('--host', help='destination IP literal override for TCP endpoints only')
    parser.add_argument('--reconnect-attempts', type=int, default=5)
    parser.add_argument('--reconnect-delay-ms', type=int, default=100)
    parser.add_argument('--timeout-ms', type=int, default=1000)
    parser.add_argument('--max-lateness-ms', type=int, default=100)
    parser.add_argument('--report', type=Path, help='new .json report path with a .md companion; existing files are refused')
    args = parser.parse_args()
    stop = threading.Event()
    interrupted = [0]

    def interrupt(number, _frame):
        interrupted[0] = interrupted[0] or number
        stop.set()

    for number in (signal.SIGINT, signal.SIGTERM):
        signal.signal(number, interrupt)
    try:
        report = stream(json.loads(args.config.read_text()), args.manifest, args.loops, args.host, stop,
            reconnect_attempts=args.reconnect_attempts, reconnect_delay_ms=args.reconnect_delay_ms,
            timeout_ms=args.timeout_ms, max_lateness_ms=args.max_lateness_ms, report_path=args.report)
    except ProducerError as error:
        print(str(error), file=sys.stderr)
        return 1
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2))
    return 128 + interrupted[0] if interrupted[0] else 0


if __name__ == '__main__':
    sys.exit(main())
