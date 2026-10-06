# Восстановление host producer и runtime отчёты

Дата: 06.10.2026. Feature commit `3109a74`, исправление total handshake deadline — `d067677`. C++ server/library, native fingerprint и RPM payload 0.6.0 не менялись. Формат/limits: [[engineering/SOURCES#Восстановление producer и отчёт]], команды — [[engineering/USAGE]].

## Проверки

| Проверка | Результат | Граница вывода |
|---|---|---|
| Полный Release CTest | 12/12 групп | Затем итоговый camera_sources повторён после handshake fix |
| camera_sources | 13/13 cases | 8 самостоятельных socket tests + 2 actual-server restart + 3 исходных source cases |
| camera_sources с явным Mesa vendor | 13/13 cases | EGL vendor `/usr/share/glvnd/egl_vendor.d/50_mesa.json` |
| Unix и TCP server restart | Все четыре source sessions сменились, READY восстановлен | Localhost; не две физические машины |
| Нет приёмника, retry budget=2 | По 3 попытки на камеру; failed report | Конечный budget, не бесконечный reconnect |
| Slow payload reader одной камеры | Send timeout закрывает её worker; остальные завершают запись | Socket fixture, не аппаратный драйвер |
| Blocked/slow-drip HELLO_ACK | Stop завершается; общий 100 ms deadline не продлевается байтами | Тестовый host, не realtime guarantee для file I/O |
| Malformed HELLO_ACK | Fatal camera error без retry | EOF при молчаливом отказе server остаётся transport error |
| SIGTERM | Exit 143 и частичные JSON/Markdown со status cancelled | Штатная остановка, не SIGKILL/crash |
| Existing report / sidecar | Отклонены до подключения, содержимое сохранено | Два новых report paths обязательны |
| Сохранённый Blender dataset | Socket validator PASS; producer reports сохранены | GPU NVIDIA RTX 5070 Ti; finite recording, не интерактивное вождение |

Native CPU ASan/UBSan и 25-критериальные platform baselines относятся к предыдущей C++ квалификации [[validation/SOURCES_SMOKE]]; здесь они не объявляются повторно измеренными. Новая реализация host-программы проверяется собственными hashes ниже.

## Actual-server restart

В каждом case: 2 scenario rows с шагом 33.333333 ms, 60 loops, 120 scheduled frames на камеру; server остановлен на 250 ms и запущен заново на тех же endpoints. Retry=60, backoff=20 ms, timeout=200 ms, max lateness=40 ms. Client получает READY до и после restart; все четыре source session ID различаются. Приведён последний прогон с явным Mesa vendor. Sent означает завершённый sendall, а не ACK использования кадра сервером. Числа skipped/errors зависят от планировщика и не являются фиксированными pass thresholds.

| Transport | Камера | Sent | Late skip | Connections | Transport errors |
|---|---:|---:|---:|---:|---:|
| unix | 0 | 112 | 8 | 2 | 14 |
| unix | 1 | 112 | 8 | 2 | 14 |
| unix | 2 | 112 | 8 | 2 | 14 |
| unix | 3 | 112 | 8 | 2 | 14 |
| tcp | 0 | 112 | 8 | 2 | 14 |
| tcp | 1 | 112 | 8 | 2 | 14 |
| tcp | 2 | 112 | 8 | 2 | 14 |
| tcp | 3 | 112 | 8 | 2 | 14 |

Implementation SHA-256: `70e1357a589d87ef2cd6dbe6857aa008aa7edc4feaba0125e4d054b39165ddd5`. Он включает `tools/producer.py` и `tools/ipc.py`; это отдельный host hash, не подмена C++ platform fingerprint. Runtime report содержит также версии Python/Pillow и config/manifest hashes. Reports сохраняются тестом в отдельных ignored build subdirectories; все первичные camera records приведены далее.

## Повторение

```sh
ctest --test-dir build -R camera_sources --output-on-failure
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build -R camera_sources --output-on-failure
python3 tests/test_producer.py
python3 tools/blender/validate.py --dataset artifacts/blender-street \
  --output artifacts/blender-producer-new --source socket
```

Путь Mesa vendor специфичен для данного ПК. Первые две команды требуют собранный server; самостоятельные восемь socket tests не требуют GPU/Blender. Output Blender validator должен быть новым.

## Blender report

`artifacts/blender-producer-final/producer.json` и одноимённый `.md`: validator получил четыре READY RGBA frames, проверил 16 input hashes и применённый front preset; затем штатно остановил producer через SIGTERM. Status `cancelled` ожидаем: запущено 200 loops, smoke завершается раньше. Число sent по камерам: 5, 5, 5, 5. Manifest SHA-256 `d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc` совпадает с сохранённой метрической улицей. Native Blender report — `artifacts/blender-producer-final/smoke.json`, PASS, renderer RTX 5070 Ti. Изображение проходит тот же dome+floor путь рисунка 3.14; качество швов этим тестом не оценивается.

## Открытые условия

Physical two-host clocks/capture skew, direct hardware capture, sensor exposure timestamps, длительные RSS/thermal/overload опыты, интерактивный Blender и UDP остаются открытыми. Source late-drop ограничивает только старт send, не байты в kernel buffers или конечный возраст экспозиции. File decode/verification не имеют жёсткой отмены. Retry budget конечный; по его исчерпании нужна новая команда запуска либо настройка budget.

## Первичный UNIX report

Локальный файл: `build/producer-unix-bobablfk/report.json`; snapshot не изменён.

```json
{
  "schema_version": 1,
  "suite_id": "surround-view-producer-v1",
  "status": "completed",
  "started_utc": "2026-10-06T20:30:05.420198+00:00",
  "clock_domain": "producer_monotonic",
  "implementation_sha256": "70e1357a589d87ef2cd6dbe6857aa008aa7edc4feaba0125e4d054b39165ddd5",
  "implementation_files": {
    "producer.py": "cb7d3b9eba0e22b3f9919efb30ea0b487a48e9e90b0d11ddac2c3847ca62ecf5",
    "ipc.py": "cbda66171dfb44e3765bdb4c3d0bca864923034e83f41ac6d393d3de0bea5ddb"
  },
  "python_version": "3.14.7",
  "pillow_version": "12.3.0",
  "manifest_sha256": "63ef682c32d81c1ae2dbc632445d91d4fa8a32308f8d41e444fbd8e7a6e96531",
  "config_sha256": "d39da31cf8d06b8d82ce0e8d5198a43a95ca01c49a920aba38d9b8b33c5fb84c",
  "settings": {
    "loops": 60,
    "reconnect_attempts": 60,
    "reconnect_delay_ms": 20,
    "timeout_ms": 200,
    "max_lateness_ms": 40
  },
  "cameras": [
    {
      "camera_id": 0,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 59.079391,
      "max_send_block_ms": 0.195561,
      "last_session_id": "camera-0-151974778639791",
      "last_error": "[Errno 2] No such file or directory"
    },
    {
      "camera_id": 1,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 59.014318,
      "max_send_block_ms": 0.197194,
      "last_session_id": "camera-1-151974778995015",
      "last_error": "[Errno 2] No such file or directory"
    },
    {
      "camera_id": 2,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 59.060606,
      "max_send_block_ms": 0.354162,
      "last_session_id": "camera-2-151974778907339",
      "last_error": "[Errno 2] No such file or directory"
    },
    {
      "camera_id": 3,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 59.908985,
      "max_send_block_ms": 0.201392,
      "last_session_id": "camera-3-151974778792090",
      "last_error": "[Errno 2] No such file or directory"
    }
  ],
  "duration_ms": 4167.145198
}
```

## Первичный TCP report

Локальный файл: `build/producer-tcp-0t7s6wou/report.json`; snapshot не изменён.

```json
{
  "schema_version": 1,
  "suite_id": "surround-view-producer-v1",
  "status": "completed",
  "started_utc": "2026-10-06T20:30:01.010900+00:00",
  "clock_domain": "producer_monotonic",
  "implementation_sha256": "70e1357a589d87ef2cd6dbe6857aa008aa7edc4feaba0125e4d054b39165ddd5",
  "implementation_files": {
    "producer.py": "cb7d3b9eba0e22b3f9919efb30ea0b487a48e9e90b0d11ddac2c3847ca62ecf5",
    "ipc.py": "cbda66171dfb44e3765bdb4c3d0bca864923034e83f41ac6d393d3de0bea5ddb"
  },
  "python_version": "3.14.7",
  "pillow_version": "12.3.0",
  "manifest_sha256": "63ef682c32d81c1ae2dbc632445d91d4fa8a32308f8d41e444fbd8e7a6e96531",
  "config_sha256": "fadb1c3e11af8376bf87528e467f6e812de5aff439537d27c2d9f8a0877174d6",
  "settings": {
    "loops": 60,
    "reconnect_attempts": 60,
    "reconnect_delay_ms": 20,
    "timeout_ms": 200,
    "max_lateness_ms": 40
  },
  "cameras": [
    {
      "camera_id": 0,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 56.273584,
      "max_send_block_ms": 0.308345,
      "last_session_id": "camera-0-151970365389430",
      "last_error": "[Errno 111] Connection refused"
    },
    {
      "camera_id": 1,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 57.243233,
      "max_send_block_ms": 0.260474,
      "last_session_id": "camera-1-151970366719613",
      "last_error": "[Errno 111] Connection refused"
    },
    {
      "camera_id": 2,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 56.905944,
      "max_send_block_ms": 0.340977,
      "last_session_id": "camera-2-151970365895139",
      "last_error": "[Errno 111] Connection refused"
    },
    {
      "camera_id": 3,
      "status": "completed",
      "scheduled_frames": 120,
      "sent_frames": 112,
      "skipped_late_frames": 8,
      "skipped_missing_frames": 0,
      "connect_attempts": 15,
      "connections": 2,
      "reconnects": 1,
      "transport_failures": 14,
      "payload_bytes": 19353600,
      "max_lateness_ms": 57.26759,
      "max_send_block_ms": 0.283588,
      "last_session_id": "camera-3-151970366494046",
      "last_error": "[Errno 111] Connection refused"
    }
  ],
  "duration_ms": 4167.166289
}
```
