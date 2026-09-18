# Архитектура стенда Surround View

## Назначение

Документ фиксирует целевую архитектуру трехпроцессной системы:

- `sv-server` работает на головном устройстве автомобиля и является владельцем обработки;
- `sv-ui` работает на головном устройстве как Qt5-клиент;
- `sv-simulator` работает на ноутбуке и имитирует автомобиль и его устройства.

Главный принцип: все варианты источников данных подключаются к одному серверному pipeline. Реальные камеры, записанные данные и симулятор не должны приводить к копированию алгоритмов обработки.

## Логическая схема

```text
                         ноутбук
┌────────────────────────────────────────────┐
│ sv-simulator                               │
│ 3D world -> vehicle -> virtual cameras     │
│           -> sensor/CAN models             │
└───────────────┬────────────────────────────┘
                │ camera / sensor / CAN / control
                ▼
┌────────────────────────────────────────────┐
│ sv-server                                  │ головное устройство
│ input adapters -> sync -> GPU Surround View│
│ configuration -> telemetry -> output       │
└───────────────┬────────────────────────────┘
                │ local rendered-frame + state
                ▼
┌────────────────────────────────────────────┐
│ sv-ui                                      │ головное устройство
│ Qt5: display + touch -> camera commands    │
└────────────────────────────────────────────┘
```

## Процессы и ответственность

### `sv-server`

Состав:

- `transport`: прием и передача сообщений;
- `input_adapters`: камеры, сенсоры, CAN, simulator;
- `time_sync`: timestamps, sequence numbers и политика рассинхронизации;
- `config`: конфигурация автомобиля, камер и поверхности;
- `geometry`: параметрическая bowl-сетка;
- `calibration`: intrinsics, extrinsics и distortion;
- `gpu`: OpenGL context, textures, shaders, framebuffers;
- `render_pipeline`: выборка, blending и итоговая сцена;
- `camera_controller`: серверное состояние виртуальной камеры;
- `output`: rendered frame и telemetry;
- `health`: ошибки, drops, reconnect и состояние сервиса.

Сервер не должен импортировать Qt Widgets. Это позволяет запускать его без графической сессии и тестировать отдельно от UI.

### `sv-ui`

Состав:

- Qt5 application и event loop;
- video sink;
- touch-to-command mapper;
- connection/status model;
- отображение telemetry и ошибок.

UI не вычисляет UV, не выполняет camera calibration и не управляет OpenGL-ресурсами серверного pipeline.

### `sv-simulator`

Состав:

- управление автомобилем;
- 3D-мир и визуализация для оператора ноутбука;
- виртуальные камеры;
- camera frame producer;
- sensor producer;
- CAN producer;
- transport client;
- recorder/replayer;
- fault injection для задержек, drops и отключений.

Симулятор не должен знать внутреннюю структуру серверного renderer. Он работает только через публичный protocol.

## Потоки данных

### Входные данные сервера

| Канал | Источник | Частота | Требование |
|---|---|---:|---|
| `camera_frame` | четыре камеры или simulator | 15-60 FPS | низкая задержка, порядок кадров |
| `sensor_sample` | simulator или сенсоры | зависит от сенсора | timestamp и порядок |
| `can_frame` | CAN adapter или simulator | зависит от шины | сохранение ID и timestamp |
| `ui_camera_command` | Qt5 UI | по событию | малая задержка |
| `configuration` | файл или control client | редко | versioned и валидируется |

### Выходные данные сервера

| Канал | Получатель | Содержание |
|---|---|---|
| `rendered_frame` | Qt5 UI | финальный кадр, timestamp, frame ID |
| `camera_state` | Qt5 UI | подтвержденное положение виртуальной камеры |
| `telemetry` | UI, simulator, logger | latency, FPS, drops, health, timings |

## Версионируемый protocol

Каждое сообщение должно иметь общий заголовок:

```text
protocol_version
message_type
source_id
sequence_id
timestamp_ns
payload_size
```

Минимальные payload-модели:

```text
camera_frame:
  camera_id
  width, height
  pixel_format
  frame_timestamp_ns
  image_payload

can_frame:
  channel
  can_id
  is_extended
  dlc
  data[0..8]
  bus_timestamp_ns

ui_camera_command:
  command_id
  type: orbit | pan | zoom | preset
  dx, dy, value
  event_timestamp_ns

rendered_frame:
  frame_id
  width, height
  pixel_format
  source_timestamp_ns
  render_timestamp_ns
  image_payload_or_handle
```

Конкретный serialization format выбрать после сравнения требований к latency и debugability. Для первого loopback-прототипа предпочтителен простой length-prefixed binary protocol или JSON для control messages и binary payload для кадров.

## Транспортная стратегия

### MVP

- между ноутбуком и сервером: TCP или ZeroMQ поверх TCP;
- для control и telemetry: отдельные логические каналы;
- для Qt5 и локального сервера: локальный IPC или loopback TCP;
- передача кадров: обычный копируемый путь, если он позволяет достичь целевого FPS.

### Оптимизация после baseline

Рассматривать shared memory, DMA-BUF, EGL image, UDP/RTP или zero-copy только после измерения:

- bandwidth;
- количество копирований;
- CPU overhead;
- end-to-end latency;
- jitter и drops.

До выбора оптимизированного транспорта зафиксировать baseline и критерии, при которых он становится необходимым.

## Синхронизация

- Использовать monotonic clock на каждом узле.
- Сохранять timestamp захвата, отправки, приема и рендера.
- Каждому кадру и сообщению назначать `sequence_id`.
- Задать допустимое окно рассинхронизации четырех камер.
- При пропуске кадра использовать явно выбранную политику: последний валидный кадр, placeholder или пропуск результата.
- Не скрывать рассинхронизацию интерполяцией без записи фактической задержки.

## Режимы запуска

### Full simulation

```text
sv-simulator --connect <server> --scenario test_drive
sv-server --config configs/simulation.json
sv-ui --connect local
```

### Replay

```text
sv-server --config configs/replay.json --replay data/recording
sv-ui --connect local
```

### Hardware

```text
sv-server --config configs/vehicle.json --hardware
sv-ui --connect local
```

Команды являются иллюстрацией интерфейса. Точные аргументы зафиксировать вместе с форматом конфигурации.

## Наблюдаемость

Сервер должен логировать:

- startup configuration и версии protocol;
- подключение и отключение каждого источника;
- camera frame drops и queue depth;
- input-to-render latency;
- GPU и CPU timing по стадиям;
- состояние virtual camera;
- ошибки конфигурации и transport;
- номер последнего принятого кадра каждого источника.

Логи и telemetry не должны блокировать render thread. Для анализа сохранять краткие binary/CSV traces на стороне сервера или simulator.

## Этапы внедрения

1. Общий header сообщений и loopback-тест.
2. Mock server с synthetic rendered frame.
3. Qt5 UI: display и touch commands.
4. Server renderer: одна текстура и виртуальная камера.
5. Simulator: четыре synthetic camera frames.
6. Bowl, calibration, fisheye и blending на сервере.
7. Sensor/CAN messages и timestamp synchronization.
8. Запись/replay и fault injection.
9. Performance baseline и транспортная оптимизация.
10. Hardware adapters и systemd deployment.

## Критерии готовности архитектуры

- каждый процесс можно собрать и запустить независимо;
- server pipeline не зависит от того, пришли данные от реального устройства или simulator;
- UI передает только команды и отображает server output;
- protocol имеет версию и sequence/timestamp поля;
- есть replay без ноутбучного 3D-мира;
- можно измерить latency на каждом переходе;
- ошибки источника диагностируются без зависания рендера;
- транспорт можно заменить без изменения геометрии и shader-кода.
