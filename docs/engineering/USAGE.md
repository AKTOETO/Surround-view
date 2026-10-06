# Работа с прототипом: от первого кадра до камер и калибровки

Сначала [[engineering/BUILD]]. Для объёмной улицы с разнесёнными камерами и движением — [[engineering/BLENDER]]. Для переносимого отчёта — [[engineering/PLATFORM_TEST]], для RPM — [[engineering/AURORA]]. Команды ниже выполняются из корня проекта; `build/` можно заменить installed `bin/`, а config — установленным файлом. CLI-программы сообщают usage при отсутствии обязательных аргументов; неизвестные параметры считаются ошибкой.

## Универсальный клиент и TCP

В 0.5.0 GUI использует `sv-client-lib`. Настройка `connections`, Unix-only/TCP-only/combined, команды запуска с Blender и внешний CMake consumer: [[engineering/CLIENT_LIBRARY]]. Без блока connections прежние Unix-команды ниже сохраняются. `state` запрашивает состояние; replay следует интервалам manifest, на wrap используется первый интервал (для одного ряда — 33.333333 ms).

## Быстрый запуск с фотографической улицей

```sh
build/sv-scene --config configs/street-demo.json \
  --panorama assets/demo/urban_street_01_1k.hdr --output artifacts/street-demo
SV_EGL_PLATFORM=surfaceless build/sv-server --config configs/street-demo.json \
  --manifest artifacts/street-demo/manifest.json --ipc-dir /tmp/sv-street \
  --trace artifacts/street-trace.jsonl
```

В другом терминале:

```sh
build/sv-client /tmp/sv-street
```

Профиль `street-demo.json` задаёт полусферический купол радиусом 12 м и круглый пол; камера обзора расположена внутри оболочки на расстоянии 8.5 м от центра. Четыре калиброванных изображения отображаются на соответствующие области пола и купола. Это проекционный стенд с общей точкой камер, а не параллаксная 3D-реконструкция; незакрытые полем зрения области заполняются фоном.

Перетаскивание изображения меняет азимут/высоту, колесо — расстояние. Пресеты: сверху, спереди, сзади. Пауза останавливает обновление входов, поворот продолжает работать; «Шаг» выбирает следующий ряд записи. «Продолжить» возобновляет replay. `Ctrl+C` завершает сервер и удаляет принадлежащие ему сокеты. Использовать свободный `--ipc-dir`; сервер не удаляет чужие существующие файлы.

Сцена содержит настоящую фотографию улицы и виртуальную ego-модель; ограничения общего оптического центра подробно объяснены в [[engineering/SCENE]]. Один ряд manifest зацикливается по умолчанию. Это интерактивный просмотр фотографии, а не живая запись камер.

## Запуск с миром из Blender

Сохранённый мир находится в `assets/scenes/metric-street/street.blend`. Сервер получает четыре изображения камер через replay-manifest; сначала нужно экспортировать их из мира и преобразовать в fisheye-входы. Blender нужен на этапе экспорта, Python с NumPy/Pillow — на этапе конвертации. Подробности и происхождение ассетов: [[engineering/BLENDER]], [[engineering/ASSETS]].

### 1. Подготовить запись из сохранённого мира

Из корня проекта:

```sh
blender --background assets/scenes/metric-street/street.blend --python-expr '
import bpy
import sys
from pathlib import Path
root = Path.cwd()
sys.path.insert(0, str(root / "tools/blender"))
import scene
scene.capture(bpy.data.scenes["SV Research Street"],
              root / "artifacts/blender-street-capture",
              frames=60, face_size=256)
'

python3 tools/blender/convert.py \
  --capture artifacts/blender-street-capture \
  --output artifacts/blender-street
```

Этот путь использует геометрию сохранённого `.blend`, включая сделанные в нём правки. `frames=60` создаёт две секунды сценарного движения при 30 FPS; offline-рендер занимает отдельное время. Для короткой проверки заменить 60 на 4. Если Blender отсутствует в PATH, указать абсолютный путь к бинарнику.

Оба output-каталога должны быть новыми. Если `artifacts/blender-street/config.json` и `manifest.json` уже подготовлены, перейти к запуску сервера. Для повторной генерации использовать другие каталоги и заменить пути в командах ниже.

Альтернатива — воссоздать исходный мир Python-скриптом и экспортировать его:

```sh
blender --background --python tools/blender/scene.py -- \
  --output artifacts/blender-street-capture --frames 60 --face-size 256
```

После этой альтернативной команды выполнить ту же конвертацию. Она создаёт новый мир из кода; правки сохранённого `.blend` загружаются первым способом.

### 2. Запустить сервер

В первом терминале:

```sh
SV_EGL_PLATFORM=surfaceless build/sv-server \
  --config artifacts/blender-street/config.json \
  --manifest artifacts/blender-street/manifest.json \
  --ipc-dir /tmp/sv-blender \
  --trace artifacts/blender-server.jsonl --loop true
```

Использовать именно config, созданный конвертером: он содержит калибровку разнесённых Blender-камер и поверхность `dome_floor_v1`. Виртуальная камера располагается внутри купола, нижняя часть сцены отображается на отдельном круглом полу. `configs/street-demo.json` относится к фотографической панораме и этому набору не соответствует.

### 3. Запустить клиент

Во втором терминале, также из корня проекта:

```sh
build/sv-client /tmp/sv-blender
```

Путь должен совпадать с `--ipc-dir` сервера. Управление: перетаскивание — ракурс, колесо — расстояние, пресеты — сверху/спереди/сзади; пауза останавливает воспроизведение. Сервер завершать через `Ctrl+C`. Для получения кадра без GUI:

```sh
python3 tools/client.py --ipc-dir /tmp/sv-blender \
  --preset front --output artifacts/blender-client-capture
```

После подготовки записи Blender можно закрыть: сервер и клиент работают с экспортированными кадрами. Движение автомобиля воспроизводится из записи; интерактивное вождение в Blender и live-передача кадров пока не реализованы.

## Выбор поверхности и fusion

Версия 0.4.0 поддерживает также цилиндр и куб с полом и крышкой, hard/edge/angular fusion и диагностические карты. Поля JSON, команды запуска comparison harness и интерактивного сервера с Blender-набором: [[engineering/RENDERING]]. Конфигурация меняется перед запуском сервера.

## Аналитическая сцена и текстовый клиент

```sh
python3 tools/simulator.py --config configs/synthetic.json \
  --output artifacts/fixture --frames 12
SV_EGL_PLATFORM=surfaceless build/sv-server --config configs/synthetic.json \
  --manifest artifacts/fixture/manifest.json --ipc-dir /tmp/sv-synthetic \
  --trace artifacts/synthetic-trace.jsonl --loop true
```

Для получения одного кадра без GUI:

```sh
python3 tools/client.py --ipc-dir /tmp/sv-synthetic \
  --preset top --output artifacts/client-capture
```

`tools/client.py` поддерживает `top`, `front`, `rear`; сохраняет PPM и metadata. `sv-client IPC_DIR --smoke` закрывается через две секунды для автоматизированной проверки; при `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` физическое окно не создаётся. GUI-метка `ui_present_submit` означает передачу кадра на представление, не подтверждает момент свечения дисплея.

Сервер использует один Asio runner-поток для control/data-сокетов и основной поток-владелец EGL. Один отправленный кадр ожидает release, дедлайн 250 ms. Python-клиент подтверждает после получения копии; Qt — после собственной копии `QImage`. Таймаут закрывает data-соединение, control остаётся доступным. Reconnect создаёт новую сессию. В 0.6.0 добавлен отдельный source worker для replay либо Unix/TCP-камер; общего worker pool нет. См. [[engineering/SOURCES]].

## Изолированный benchmark и изображение

```sh
build/sv-bench --config configs/street-demo.json \
  --manifest artifacts/street-demo/manifest.json --output artifacts/street-view \
  --egl-platform surfaceless --warmup 10 --iterations 60
```

| Аргумент `sv-bench` | Назначение |
|---|---|
| `--config`, `--manifest` | Обязательные входы; берётся первый ряд записи |
| `--output` | Каталог `effective_config.json`, `projection.json`, `metrics.json`, `preview.ppm` |
| `--egl-platform` | `surfaceless`, `device` или `default` |
| `--warmup`, `--iterations` | Прогрев и число измерений |
| `--height` | Высота угла bowl в метрах; 0 — плоскость |
| `--cells` | Число ячеек по обеим осям |
| `--width`, `--output-height` | Размер результата, не входных камер |

`sv-bench` оставлен компактным инструментом одного опыта. `gpu_time_ms` его прежнего metrics-контракта остаётся null; расширенные GPU/upload/readback показатели создаёт **`sv-platform-test`**, см. [[engineering/PLATFORM_TEST]]. Не смешивать результаты разных scope. Для аппаратного NVIDIA использовать `--egl-platform device`; при нескольких devices — `SV_EGL_DEVICE`. Для обязательного Mesa задать `LIBGL_ALWAYS_SOFTWARE=1`. Всегда смотреть фактический `gl_renderer`.

## Запись четырёх источников через OpenCV

```sh
build/sv-capture --config configs/synthetic.json --output artifacts/cameras-01 \
  --source0 /dev/video0 --source1 /dev/video1 \
  --source2 /dev/video2 --source3 /dev/video3 --frames 30
```

Каждый source — номер устройства (`0`), путь устройства либо файл видео. Доступный backend выбирает OpenCV `CAP_ANY`, его имя сохраняется в `capture.json`. `grab()` вызывается для всех четырёх входов, затем `retrieve()`; сохраняются PNG, SHA-256, manifest, времена доставки и skew. Файловые источники и порядок формата проверены тестом `vision_tools`; доступ к физическим камерам Авроры пока не проверен.

Встроенная synthetic calibration **не подходит реальному автомобилю**: перед записью указать реальные разрешения, intrinsics/extrinsics и calibration IDs. Запрос `CAP_PROP_FRAME_WIDTH/HEIGHT` может быть проигнорирован драйвером; программа проверяет фактический размер и отказывает при расхождении. Не выполняет скрытый resize/crop, потому что он меняет калибровку.

Метки — host steady-clock **после retrieve**, а не аппаратные exposure timestamps. Цикл включает последовательный захват и запись на диск; это recorder для подготовки данных, не realtime producer. Replay worker воспроизводит интервалы manifest; file decode и scheduling добавляют задержку, аппаратная синхронность не гарантируется. EOF одного входа или неправильный формат завершает запись ошибкой; неполный каталог не выдаётся за успешный набор.

Затем запустить `sv-bench`/`sv-server` с теми же config и `artifacts/cameras-01/manifest.json`. Для проверки всех hashes использовать `sv-platform-test --manifest ...`: обычный replay-loader сейчас проверяет IDs и формат, но не сканирует все хэши заранее.

## OpenCV: детекция калибровочного шаблона

```sh
build/sv-calibrate detect --image data/board.png --columns 9 --rows 6 \
  --square-size-m 0.03 --output artifacts/board-detection
```

`columns`/`rows` — число **внутренних углов**, а не клеток. `square-size-m` — измеренный размер стороны клетки, в метрах. Результат: `detected.png` с обнаруженными углами и `detections.json` с image SHA-256, UV и XYZ в системе доски. Это **не** координаты автомобиля. Пустой/неподходящий снимок отвергается; ручной ввод углов вместо фактической детекции не выполняется.

Детектор: `cv::findChessboardCornersSB`, grayscale и normalize-image. Точность углов на синтетических проецированных изображениях проверяется вместе с полным fitting CLI; качество на снимках реального объектива нужно оценить отдельно. Стороны доски, неоднозначность её ориентации и измеренную привязку к автомобилю фиксировать до внешней калибровки.

## OpenCV: внутренняя калибровка по снимкам

Создать dataset JSON рядом с фотографиями:

```json
{
  "schema_version": 1,
  "camera_id": 0,
  "theta_max_rad": 1.45,
  "board": {"columns": 9, "rows": 6, "square_size_m": 0.03},
  "train": ["train/01.png", "train/02.png", "train/03.png",
            "train/04.png", "train/05.png", "train/06.png"],
  "validation": ["validation/01.png", "validation/02.png", "validation/03.png"]
}
```

Это пример структуры; перечисленные реальные файлы подготовить самостоятельно. Пути относительно dataset. Нужно 6..200 обучающих и 3..200 проверочных кадров одной камеры и одинакового размера. Варьировать наклон, расстояние и расположение доски, покрывая рабочий угол объектива. Разделение сделать **до** оптимизации. Повторение файла/байтов, в том числе под другим именем, отвергается по SHA-256.

```sh
build/sv-calibrate intrinsics --dataset data/calibration/dataset.json \
  --output artifacts/intrinsics-front --max-error-px 1.0
```

Оцениваются fx, fy, cx, cy, k1…k4 при alpha=0 реальным `cv::fisheye::calibrate`; включены recompute-extrinsic, condition check, fixed skew. Проверяются конечность, положительные focal и производная радиального отображения на 1025 углах `[0,theta_max_rad]`. При загрузке полного config дополнительно проверяются стационарные точки полинома; сеточная проверка fitter не заменяет её. По умолчанию theta_max=1.45; сужать домен допустимо только как отдельный обоснованный профиль. Данные центра кадра не обосновывают экстраполяцию на всю периферию: чрезмерные k или немонотонность требуют пересъёмки/изменения модели, а не ослабления проверок.

На validation intrinsics фиксированы; поза доски оценивается отдельно через fisheye undistortion + `solvePnP`, затем считается остаток репроекции. Поэтому held-out p95 — ошибка с оцененной позой доски, **не метрическая ошибка независимых точек автомобиля**. При p95≤порога экспортируются `intrinsics.json`, `report.json` и аннотированные снимки; иначе report имеет rejected и модель не экспортируется. Exit 0 — accepted, 2 — rejected, 1 — ошибка входа/детектора/оценивания. Каталог output должен быть новым.

`intrinsics.json` содержит также resolution. В camera `projection` переносить только поля модели (`model`, fx/fy/cx/cy, alpha, k, theta_max, z_epsilon); размер отдельно в `resolution`. Внешнюю матрицу эта команда не определяет. Сформировать **новый** calibration ID и согласовать manifest; не подставлять прежний analytic ID под реальные параметры.

## Внешняя калибровка и диагностика по известным XYZ

`tools/configurator.py` — отдельный экспериментальный NumPy LM/Huber solver, сохранённый для воспроизводимой оценки общей модели по известным пространственным соответствиям. На каждую из четырёх камер нужны `train` и `validation`, ≥30 точек, массивы `points` N×3 в системе автомобиля и `pixels` N×2. Совокупная геометрия должна быть неплоской; dataset из неизвестных поз доски нельзя автоматически считать таким набором.

```sh
python3 tools/configurator.py calibrate --config configs/synthetic.json \
  --observations artifacts/experiments-rtx/observations.json \
  --output artifacts/calibration-new --threshold 1.0
python3 tools/configurator.py diagnose --config artifacts/calibration-new/calibrated.json \
  --observations artifacts/experiments-rtx/observations.json \
  --output artifacts/diagnostic --threshold 1.0
```

Пример observations создаётся `run_experiments.py`; его synthetic значения не применять к настоящим камерам. Solver проверяет rank, held-out ошибку и монотонность; report сохраняет исходный и конечный остаток, condition и hashes. Диагностика известного заднего смещения остаётся исследовательским сценарием; overlap detector и статистика ложных тревог реального движения не реализованы.

## Форматы и соглашения

Основной config — строгий JSON `schema_version=1`, именованный `profile_id`. Единицы m/rad/ns; V: X вперёд, Y влево, Z вверх; C: X вправо, Y вниз, Z вперёд. `T_camera_from_vehicle` — row-major 4×4, умножается на column-vector. Alpha=0, четыре k, положительный Z, theta<π/2. Подробная математика: [[architecture/MATHEMATICS]], actual пример — `configs/synthetic.json`.

Manifest: `schema_version`, четыре calibration IDs, `frames` с десятичной строкой `scenario_timestamp_ns`, четырьмя `paths`/`offset_ns`, объектом `sha256` на верхнем уровне. Null path означает отсутствующий вход; offset до ±1 s задаёт искусственный skew. Изображения декодируются OpenCV из PNG/JPEG либо строгим P6-reader из PPM; внутри RGB8 top-left. GPU-результат — RGBA8 top-left. Нет аппаратного YUV/zero-copy адаптера.

## Отчёты, повторение и типичные ошибки

Полная квалификация и сравнение — [[engineering/PLATFORM_TEST]]. Исторический Linux сценарий:

```sh
python3 tools/run_experiments.py --build build --platform device \
  --output artifacts/experiments-new --repeats 3 --iterations 60
python3 docs/diploma/plot_experiments.py --previews artifacts/experiments-new
```

`plot_experiments.py` по умолчанию строит исторические графики из [[prototype/MEASUREMENTS]]; новые цифры сначала оформить отдельным Markdown-отчётом и явным образом связать с рисунком. Новые схемы/стрит-кадры воспроизводятся `plot_system.py`, native графики — `plot_platform.py`, см. [[diploma/README]]. Большие записи и traces — в `artifacts`/`data`, а интерпретация и первичные отчёты — в тематических `docs`.

| Сообщение / симптом | Действие |
|---|---|
| CMake не нашёл пакет | Установить development-пакет в выбранный host/target; CMake не загружает его |
| EGL initialization/context/FBO | Проверить backend и доступ процесса к GPU; CPU-only report сохраняет остальные проверки |
| `manifest calibration mismatch` | Проверить четыре IDs; нельзя переименовывать калибровку без обновления данных |
| Capture resolution differs | Использовать поддерживаемое исходное разрешение и соответствующую калибровку |
| `chessboard not detected` | Проверить число внутренних углов, видимость всей доски, резкость и экспозицию |
| Nonmonotonic/ill-conditioned calibration | Расширить покрытие/наклоны, переснять, проверить модель и размер клетки |
| IPC paths already exist | Выбрать новый каталог, проверить прежний процесс и владельца |
| Report comparison refuses ratio | Прочитать reasons; повторить ПК с той же версией кода/нагрузкой, сохранить старую базу |

Остановленный процесс не исправлять слепым удалением сокетов чужой сессии. Порог ошибки или hash-check не отключать ради сравнения. В [[prototype/STATUS]] перечислены отличия Linux-профиля от полной проектной системы.

## Blender-запись через виртуальные камеры

После экспорта `artifacts/blender-street` по [[engineering/BLENDER]] сервер можно запустить без replay manifest. Подготовьте deployment config на основе **конфига данного dataset**, чтобы сохранить mounts, intrinsics, calibration IDs и купол. Команды из корня checkout:

```sh
python3 - <<'PY'
import json
from pathlib import Path
config = json.loads(Path('artifacts/blender-street/config.json').read_text())
config['connections'] = {'unix': {'enabled': True, 'directory': '/tmp/sv-virtual-client'}}
config['source'] = {'type': 'socket', 'message_timeout_ms': 2000, 'cameras': [
    {'camera_id': i, 'transport': 'unix', 'path': f'/tmp/sv-virtual-input/camera{i}.sock'}
    for i in range(4)]}
Path('artifacts/blender-street/socket-config.json').write_text(json.dumps(config, indent=2))
PY
build/sv-server --config artifacts/blender-street/socket-config.json \
  --trace artifacts/blender-virtual/server.jsonl
```

Во втором терминале запустите GUI; в третьем — producer:

```sh
build/sv-client --ipc-dir /tmp/sv-virtual-client
python3 tools/producer.py --config artifacts/blender-street/socket-config.json \
  --manifest artifacts/blender-street/manifest.json --loops 100
```

Для headless-проверки GUI заменяется `build/sv-client-probe --unix /tmp/sv-virtual-client --frames 10`. До подключения producer состояние NO_INPUT ожидаемо; после завершения записи данные устаревают. Pause фиксирует текстуры, orbit использует их повторно; step для socket source отклоняется. Процесс producer продолжает отправлять на паузе, сервер отбрасывает входы.

Для разных машин измените `source.cameras` на четыре TCP endpoints с явным адресом интерфейса **сервера** и портами 48080…48083. На ноутбуке передайте копию config, dataset и запустите producer с `--host SERVER_IP`. Для удалённого GUI также явно включите `connections.tcp` с отдельными control/data портами по [[engineering/CLIENT_LIBRARY]]; camera ports для клиента не подходят. Сами устройства камер в будущем будет открывать server-side adapter, а не GUI ноутбука. Детальный формат, ограничения времени/памяти и безопасность — [[engineering/SOURCES]]. Физическое двухмашинное испытание ещё не проведено.
