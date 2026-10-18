# Работа с прототипом: от первого кадра до камер и калибровки

Сначала [[engineering/BUILD]]. Для объёмной улицы с разнесёнными камерами и движением — [[engineering/BLENDER]]. Для переносимого отчёта — [[engineering/PLATFORM_TEST]], для RPM — [[engineering/AURORA]]. Команды ниже выполняются из корня проекта; `build/` можно заменить installed `bin/`, а config — установленным файлом. CLI-программы сообщают usage при отсутствии обязательных аргументов; неизвестные параметры считаются ошибкой.

## Универсальный клиент и TCP

В 0.5.0 GUI использует `sv-client-lib`. Настройка `connections`, Unix-only/TCP-only/combined, команды запуска с Blender и внешний CMake consumer: [[engineering/CLIENT_LIBRARY]]. Без блока connections прежние Unix-команды ниже сохраняются. `state` запрашивает состояние; replay следует интервалам manifest, на wrap используется первый интервал (для одного ряда — 33.333333 ms).

Laptop GUI принимает `--unix DIR` или `--tcp HOST CONTROL_PORT DATA_PORT`; без аргументов он ищет локальные Unix IPC endpoints в ограниченном списке стандартных мест. Панель соединения позволяет редактировать endpoint, timeout/reconnect/retries, а Qt сохраняет последние значения. GUI показывает frame/source metrics и формы ConfigService fusion/surface/telemetry. Сервер поддерживает hot telemetry settings и typed polling; CPU/RSS/render tabs, loss и fresh-only render quantiles доступны в GUI: [[engineering/TELEMETRY]]. Подробности discovery и его ограничения: [README sv-simulator](../../examples/sv-simulator/README.md).

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

После подготовки записи Blender можно закрыть: сервер и клиент работают с экспортированными кадрами. Для интерактивного визуального вождения откройте вкладку World в `sv-simulator`; Qt Quick 3D preview управляется клавиатурой. Эта сцена пока изолирована от `sv-server`: она не формирует синхронные live-кадры или calibration observations. Blender-запись по-прежнему поступает в сервер как replay либо через четыре producer endpoints.

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

## Запуск сервера с локальными V4L2 камерами

`configs/v4l2-camera.json` показывает формат `source.type=camera` и четыре `/dev/video*`. До запуска проверьте пути через `v4l2-ctl --list-devices` и измените `devices` на реальные устройства. Пример содержит **синтетические** разрешения 320×180, intrinsics и extrinsics: замените их измеренными значениями, иначе изображение будет захвачено, но геометрия результата будет неверной.

```sh
SV_EGL_PLATFORM=surfaceless build/sv-server \
  --config configs/v4l2-camera.json --ipc-dir /tmp/sv-v4l2
```

Во втором терминале:

```sh
build/sv-client /tmp/sv-v4l2
```

Сервер преобразует BGR, grayscale или BGRA в RGB8; кадр с размером, отличным от config, отбрасывается. Live capture не поддерживает `step`. Остановка ждёт текущий вызов OpenCV `VideoCapture::read`, поэтому задержка зависит от драйвера. Реальные камеры в этой среде не подключались; этот пример документирует способ запуска и не является hardware acceptance.

Затем запустить `sv-bench`/`sv-server` с теми же config и `artifacts/cameras-01/manifest.json`. Для проверки всех hashes использовать `sv-platform-test --manifest ...`: обычный replay-loader сейчас проверяет IDs и формат, но не сканирует все хэши заранее.

## OpenCV: детекция калибровочного шаблона

```sh
build/sv-calibrate detect --image data/board.png --columns 9 --rows 6 \
  --square-size-m 0.03 --output artifacts/board-detection
```

`columns`/`rows` — число **внутренних углов**, а не клеток. `square-size-m` — измеренный размер стороны клетки, в метрах. Результат: `detected.png` с обнаруженными углами и `detections.json` с image SHA-256, UV и XYZ в системе доски. Это **не** координаты автомобиля. Пустой/неподходящий снимок отвергается; ручной ввод углов вместо фактической детекции не выполняется.

Детектор: `cv::findChessboardCornersSB`, grayscale и normalize-image. Точность углов на синтетических проецированных изображениях проверяется вместе с полным fitting CLI; качество на снимках реального объектива нужно оценить отдельно. Стороны доски, неоднозначность её ориентации и измеренную привязку к автомобилю фиксировать до внешней калибровки.

## OpenCV: внешняя калибровка по изображениям

`board-observations` обнаруживает внутренние углы в кадрах всех четырёх камер и переводит метрические координаты доски в систему автомобиля. Каждая позиция доски должна быть независимо измерена относительно автомобиля. Для проверки строгого noncoplanar-профиля дайте каждой камере несколько кадров с разными, неплоско расположенными в совокупности позициями доски.

Входной JSON `board-captures.json` задаёт внутренние углы, размер клетки и кадры с `T_vehicle_from_board`:

```json
{
  "schema_version": 1,
  "board": {"inner_corners": [9, 6], "square_size_m": 0.03},
  "cameras": [
    {"id": 0, "views": [
      {"image": "cam0-position1.png", "corner_order": "normal",
       "T_vehicle_from_board": [[1,0,0,4], [0,1,0,2], [0,0,1,1], [0,0,0,1]]},
      {"image": "cam0-position2.png", "corner_order": "reverse_x",
       "T_vehicle_from_board": [[1,0,0,3], [0,0.8660254,-0.5,4], [0,0.5,0.8660254,1], [0,0,0,1]]}
    ]},
    {"id": 1, "views": [
      {"image": "cam1-position1.png", "corner_order": "normal",
       "T_vehicle_from_board": [[1,0,0,4], [0,1,0,2], [0,0,1,1], [0,0,0,1]]},
      {"image": "cam1-position2.png", "corner_order": "normal",
       "T_vehicle_from_board": [[1,0,0,3], [0,0.8660254,-0.5,4], [0,0.5,0.8660254,1], [0,0,0,1]]}
    ]},
    {"id": 2, "views": [
      {"image": "cam2-position1.png", "corner_order": "reverse_y",
       "T_vehicle_from_board": [[1,0,0,4], [0,1,0,2], [0,0,1,1], [0,0,0,1]]},
      {"image": "cam2-position2.png", "corner_order": "reverse_xy",
       "T_vehicle_from_board": [[1,0,0,3], [0,0.8660254,-0.5,4], [0,0.5,0.8660254,1], [0,0,0,1]]}
    ]},
    {"id": 3, "views": [
      {"image": "cam3-position1.png", "corner_order": "normal",
       "T_vehicle_from_board": [[1,0,0,4], [0,1,0,2], [0,0,1,1], [0,0,0,1]]},
      {"image": "cam3-position2.png", "corner_order": "reverse_x",
       "T_vehicle_from_board": [[1,0,0,3], [0,0.8660254,-0.5,4], [0,0.5,0.8660254,1], [0,0,0,1]]}
    ]}
  ]
}
```

Матрица преобразует координаты шаблона `(x·square, y·square, 0)` в метры автомобиля. Допустимы `corner_order`: `normal`, `reverse_x`, `reverse_y`, `reverse_xy`. Обычная шахматная доска симметрична: порядок углов нужно сверить с меткой ориентации на шаблоне и нумерацией углов в `annotated/cameraN_viewM.png`. Неверный порядок систематически портит привязку, хотя детектор сообщает успех. Для реальной установки используйте доску с асимметричной меткой ориентации; автоматическое чтение такой метки пока не реализовано.

```sh
build/sv-calibrate board-observations --config configs/synthetic.json \
  --input data/board-captures.json --output artifacts/board-observations
build/sv-calibrate extrinsics --config configs/synthetic.json \
  --observations artifacts/board-observations/observations.json \
  --method ransac_epnp_lm --output artifacts/board-pose-candidate
```

Первый шаг пишет observations, распознанные углы с номерами, SHA-256 и отчёт об OpenCV. Второй экспортирует candidate extrinsics. Online apply выполняется только через server calibration job: сервер требует независимые train/validation и quality gate, поэтому не копируйте candidate напрямую в активный config. Углы должны относиться к config-разрешению, detector работает по исходному изображению без resize/crop. Первый image-derived опыт на Blender RGB: [[validation/IMAGE_CALIBRATION]]. Точная wire-последовательность: [[engineering/PROTOCOL_IMPLEMENTED]]. Таблица сравнения четырёх solver methods выше по-прежнему основана на аналитических XYZ/UV.

Чтобы повторить опыт с Blender доской, выполните из корня проекта (output-каталоги должны быть новыми):

```sh
blender --background --python tools/blender/scene.py -- \
  --scenario assets/scenarios/mount-errors-v1.json \
  --output artifacts/board-capture --frames 4 --face-size 512 --calibration-boards
python3 tools/blender/convert.py --capture artifacts/board-capture \
  --output artifacts/board-data
python3 tools/configurator.py calibrate-images --dataset artifacts/board-data \
  --output artifacts/board-calibration --build build --training-frames 3
```

Dataset содержит train/validation observations, annotations, candidate config и Markdown/JSON отчёт. Настройка capture, работа через Blender MCP и границы воспроизводимости: [[engineering/BLENDER#Изображения калибровочной доски]].

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
  --manifest artifacts/blender-street/manifest.json --loops 100 \
  --reconnect-attempts 20 --reconnect-delay-ms 100 --timeout-ms 1000 \
  --max-lateness-ms 100 --report artifacts/blender-virtual/producer.json
```

Для headless-проверки GUI заменяется `build/sv-client-probe --unix /tmp/sv-virtual-client --frames 10`. До подключения producer состояние NO_INPUT ожидаемо; после завершения записи данные устаревают. Pause фиксирует текстуры, orbit использует их повторно; step для socket source отклоняется. Процесс producer продолжает отправлять на паузе, сервер отбрасывает входы.

Для разных машин измените `source.cameras` на четыре TCP endpoints с явным адресом интерфейса **сервера** и портами 48080…48083. На ноутбуке передайте копию config, dataset и запустите producer с `--host SERVER_IP`. Для удалённого GUI также явно включите `connections.tcp` с отдельными control/data портами по [[engineering/CLIENT_LIBRARY]]; camera ports для клиента не подходят. Сами устройства камер в будущем будет открывать server-side adapter, а не GUI ноутбука. Детальный формат, ограничения времени/памяти и безопасность — [[engineering/SOURCES]]. Физическое двухмашинное испытание ещё не проведено.

Producer автоматически восстанавливает каждую камеру после разрыва/перезапуска сервера в пределах заданного budget. После простоя пропускает устаревшие ряды. `producer.json` и `producer.md` сохраняют sent/skip/error/reconnect counters; повторный запуск требует нового имени отчёта. Для SIGINT/SIGTERM отчёт частичный, exit 130/143 ожидаем. Семантика счётчиков, limits и отличие sent от server processing — [[engineering/SOURCES#Восстановление producer и отчёт]]. Для запуска отдельных Python socket tests без GPU: `python3 tests/test_producer.py`.

## Аналитический эталон купола и пола

Для исследования геометрии без EGL используйте CPU-путь:

```sh
python3 tools/reference.py --config artifacts/blender-street/config.json \
  --manifest artifacts/blender-street/manifest.json --output artifacts/reference-demo
```

Он создаёт цвет, coverage, четыре веса, точки на носителе и JSON/Markdown отчёт. Пять носителей и три fusion-режима сравниваются через `tools/compare_reference.py`; команды, форматы и ограничения — [[engineering/RENDERING#Независимый аналитический CPU-эталон]]. Это offline исследовательская утилита; `sv-server` продолжает использовать GPU renderer.


## svctl и примеры клиентов

`sv-client` теперь собирается из `examples/sv-client/`: крупные кнопки ракурсов/масштаба, orbit касанием, панель текущего state. Для инженерных replay-команд используйте `svctl`, работающий через ту же библиотеку без Qt:

```sh
build/svctl --unix /tmp/sv-blender state
build/svctl --unix /tmp/sv-blender pause
build/svctl --unix /tmp/sv-blender step
build/svctl --unix /tmp/sv-blender preset top
build/svctl --unix /tmp/sv-blender resume
build/svctl --tcp 127.0.0.1 53101 53102 orbit 0.1 0
```

Вывод — JSON ACK; exit 0 accepted, 2 arguments, 3 connection/timeout, 4 rejected. По умолчанию общий deadline 5000 ms; `--timeout-ms` задаётся перед командой. GUI следует закрыть перед запуском CLI: сервер пока односессионный. CLI открывает legacy data channel и освобождает кадры; calibration job commands доступны через generic command path. Typed ConfigService fusion/surface/telemetry и telemetry polling реализованы; subscriptions и полный покадровый trace остаются в плане. Библиотека не повторяет CLI мутацию после потери соединения. `sv-simulator` поддерживает Qt 6 GUI и визуальную driving scene, но live camera production и редактор наблюдений ещё не реализованы.

## Мир с погрешностями крепления и сравнение калибровки

Рецепт `assets/scenarios/mount-errors-v1.json` задаёт seed, yaw/pitch/slide и world rules; сохранённая сцена — `assets/scenes/mount-errors/street.blend`. Генерация/экспорт — [[engineering/BLENDER#Правила мира и ошибки крепления камер]]. После конвертации запустить сервер и GUI с фактическими позами:

```sh
SV_EGL_PLATFORM=surfaceless build/sv-server --config artifacts/blender-mount-v1/config.json \
  --manifest artifacts/blender-mount-v1/manifest.json --ipc-dir /tmp/sv-mount
```

В отдельном терминале:

```sh
build/sv-client /tmp/sv-mount
```

Для исследования восстановления из nominal poses:

```sh
python3 tools/configurator.py compare-mounts --dataset artifacts/blender-mount-v1 \
  --output artifacts/mount-comparison-new --trials 5 --render
```

Output directory должен быть новым. Numeric experiment работает без Blender, если dataset уже экспортирован; `--render` требует EGL/GLES. Полный observation schema, методы и ограничения — [[research/MOUNT_CALIBRATION]], результаты — [[validation/MOUNT_CALIBRATION]]. Сервер принимает `calibrate` через generic `svctl command`/library API: передаются JSON-массивы `points`, `pixels`, `validation_points`, `validation_pixels` и `camera_id`; каждая точка XYZ, каждый пиксель UV. Validation observations должны быть отложены до подгонки. Затем опрашивают `calibration_status` по `job_id`; `cancel_calibration` отменяет job, `apply_calibration` применяет его только после quality gate. Jobs привязаны к session ID, сохраняют стартовую revision config и отменяются при закрытии control-соединения; применение устаревшего job отклоняется, новая calibration получает новый ID. Текущий OpenCV вызов не прерывается, его результат после отмены отбрасывается. Применение блокируется при validation RMSE >3 px или максимуме >8 px. Пороги временные, API пока не типизирован и не проверен на физических камерах.

## Растровое исследование detector/calibrator и ограничение порядка модели

```sh
cmake --build build --target sv-calibrate -j 4
python3 tools/calibration/raster_study.py --output artifacts/calibration-raster-repeat --seeds 2401 2402
build/sv-calibrate intrinsics --dataset artifacts/calibration-raster-repeat/2402-clean/dataset.json --output artifacts/intrinsics-order2 --distortion-order 2
```

`--distortion-order` принимает строго `2` или `4`, default `4`. Профиль `2` фиксирует k3/k4=0 при оценивании k1/k2; p95 gate и monotonicity checks сохраняются. Ошибка не приводит к автоматическому fallback или ослаблению проверок. Выбор порядка требует собственных validation данных. Полный протокол, ограничения и фактические отказы: [[validation/RASTER_CALIBRATION]]. Python cv2 не требуется, используется production C++ OpenCV бинарный файл.

## Provenance для серверной calibration job

Асинхронная SV01 команда `calibrate` требует `provenance.dataset_id`, массивы `training.observation_ids/frame_ids` и `validation.observation_ids/frame_ids` той же длины/порядка, что XYZ/UV. Frame IDs двух split не должны пересекаться; для разных углов одного снимка frame ID одинаковый, observation IDs разные. Не назначайте новые frame IDs crop/augmentation одного исходного снимка. Exact копии соответствий отклоняются даже с новыми labels. При отсутствии metadata сервер возвращает `calibration_provenance_required`; это намеренное ужесточение calibration контракта, capability `calibration_provenance_v1` объявляется в handshake.

Форма JSON, причины отказа и audit в status: [[engineering/PROTOCOL_IMPLEMENTED]], тесты и пределы гарантии: [[validation/CALIBRATION_PROVENANCE]]. Offline CLI `sv-calibrate intrinsics/extrinsics` этим wire изменением не затронут. Generic command API `sv-client-lib` передаёт metadata как JSON; typed calibration service остаётся дальнейшей задачей.

## Optical-family исследование и метрическая проверка

```sh
python3 tools/configurator.py compare-optics --output artifacts/calibration-optics-repeat --seeds 3101 3102
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_optics_calibration.py --results artifacts/calibration-optics-repeat
ctest --test-dir build -R 'optical_models|raster_calibration|vision_tools' --output-on-failure
```

Требуются собранный `build/sv-calibrate` и Python NumPy/SciPy/Pillow/Matplotlib. Python cv2/Blender не требуются. Output должен быть новым. Сценарий создаёт сами растровые изображения четырёх optical families, сохраняет detector/fit failures и сравнивает экспортированные модели на известной геометрии без pose fitting. Протокол и пределы выводов: [[validation/OPTICAL_FAMILY_CALIBRATION]].

## Парное сравнение центральных и периферийных калибровочных кадров

```sh
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py compare-coverage --output artifacts/calibration-coverage-repeat --seeds 4101 4102
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_coverage.py --results artifacts/calibration-coverage-repeat
ctest --test-dir build -R 'calibration_coverage|optical_models|raster_calibration' --output-on-failure
```

Output должен быть новым. Используются NumPy/SciPy/Pillow/Matplotlib и production OpenCV C++ binary, Python cv2/Blender не нужны. Параметр `--binary` позволяет указать другой собранный `sv-calibrate`. По умолчанию создаются 256 PNG, 64 fit/gate attempts и summary с hashes. Central/wide получают одинаковое число train views, одинаковые контрольные лучи/плоскость и общую validation. Gate сохраняет свой порог; результаты автоматически не применяются к конфигу сервера. Ограничения scale/tilt/coverage и полная таблица: [[validation/CALIBRATION_COVERAGE]].

## Парный factorial distance × tilt для калибровки

```sh
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py compare-capture --output artifacts/calibration-capture-repeat --seeds 7101 7102
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_capture.py --results artifacts/calibration-capture-repeat
ctest --test-dir build -R 'calibration_capture_factors|calibration_coverage|optical_models' --output-on-failure
```

Требуются Python NumPy/SciPy/Pillow/Matplotlib и собранный C++ OpenCV calibrator. `--binary` задаёт другой путь к `sv-calibrate`. Output новый; по умолчанию создаются 384 PNG/128 fit attempts, source/binary hashes контролируются до/после запуска. Во время серии не изменяйте перечисленные в summary исходники и binary: в этом случае сценарий откажется выпускать summary. Серверный конфиг не изменяется. Протокол: [[research/CALIBRATION_CAPTURE_PROTOCOL]], результаты/ограничения: [[validation/CALIBRATION_CAPTURE_FACTORS]].

## Диагностика intrinsics: точные углы против detector

При наличии исходного capture:

```sh
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py diagnose-capture --input artifacts/calibration-capture-v1 --output artifacts/calibration-diagnostic-repeat
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_intrinsic_diagnostics.py --results artifacts/calibration-diagnostic-repeat --input artifacts/calibration-capture-v1
ctest --test-dir build -R 'intrinsic_diagnostics|vision_tools|optical_models' --output-on-failure
```

Если capture отсутствует, сначала выполните `compare-capture` из предыдущего раздела и передайте его output через `--input`. Нужны source PNG, detections и summary, созданные этим сценарием. Проверяются hashes и неизменность numerical core; 64 source cases становятся 192 exact/float32/detected fits. Во время запуска не изменяйте исходники/binary/parent inputs. Python cv2/Blender не нужны, остальные Python зависимости прежние. Output должен быть новым.

Отдельный запуск C++ diagnostic на JSON, созданном сценарием:

```sh
build/sv-calibrate diagnose-intrinsics --dataset artifacts/calibration-diagnostic-repeat/kb_nonzero-7101/small_front-analytic_truth.json --output artifacts/manual-intrinsic-diagnostic --distortion-order 4
```

Вход: `schema_version=1`, `purpose=intrinsic_solver_diagnostic`, `observation_origin=analytic_truth|analytic_truth_float32|detected_raster|controlled_perturbation`, `resolution={width,height}`, `board={columns,rows,square_size_m}`, `theta_max_rad`, массивы `train` и `validation`. Каждый view содержит `id` и `uv_px` — массив пар координат в row-major board order. Требуются 6..200 train и 3..200 validation views, уникальные непустые IDs во всём dataset, columns/rows=3..31, resolution axes=1..16384, finite UV внутри кадра, положительный размер клетки и 0<theta_max<π/2. Максимальный input — 8 MiB. Origin/IDs заявлены вызывающей стороной и не подтверждают физическое происхождение данных.

Команда пишет **только `diagnostics.json`**, содержащий `diagnostic_only`, residual и embedded estimate. Она не выполняет acceptance gate, не пишет `intrinsics.json`, не применяет конфиг. `--max-error-px` здесь не допускается. Exit 0 означает завершённую диагностику, а не принятую калибровку; solver/validation/input errors дают exit 1. Production `intrinsics` сохраняет прежний detector/gate workflow. Протокол и результаты: [[research/INTRINSIC_DIAGNOSTIC_PROTOCOL]], [[validation/INTRINSIC_DIAGNOSTICS]].

## Направленная чувствительность к equal-RMS ошибкам углов

При наличии parent diagnostic datasets:

```sh
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py compare-sensitivity --input artifacts/calibration-diagnostic-v2 --capture-summary docs/validation/baselines/calibration_capture_v1.json --output artifacts/calibration-sensitivity-repeat
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_sensitivity.py --results artifacts/calibration-sensitivity-repeat --input artifacts/calibration-diagnostic-v2
ctest --test-dir build -R 'calibration_sensitivity|intrinsic_diagnostics|vision_tools' --output-on-failure
```

Если parent отсутствует, восстановите capture/diagnostic из предыдущих разделов. Для нового parent передайте соответствующий capture `summary.json` через `--capture-summary`: его hash должен совпадать с parent diagnostic metadata. Output новый. Требуются NumPy/SciPy/Pillow/Matplotlib и C++ OpenCV binary, без Python cv2/Blender. Исторические hashes требуют соответствующего numerical core/toolchain. Во время запуска не изменяйте source/binary/parent datasets.

По умолчанию объявленная matched-model серия создаёт 444 diagnostic reports и 216 signed pair responses. Artificial UV имеют origin `controlled_perturbation`, не помечаются как detector/analytic observations. Gate/конфиг не меняются. Величина h — global RMS длины 2D input error; gains имеют единицы px/px или м/px и не являются condition number или actual per-fit error. Протокол: [[research/CALIBRATION_SENSITIVITY_PROTOCOL]], результаты: [[validation/CALIBRATION_SENSITIVITY]].

## Совместный Jacobian и неопределённость калибровки

Воспроизвести заранее заданные 12 matched cases (24 joint fits) из frozen capture/diagnostic datasets:

```bash
python3 tools/configurator.py compare-information --output artifacts/calibration-joint-information-repeat
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_joint_information.py --results artifacts/calibration-joint-information-repeat
python3 tests/test_joint_information.py
ctest --test-dir build -R 'joint_calibration_information|intrinsic_diagnostics|calibration_sensitivity' --output-on-failure
```

Нужны Python NumPy/SciPy/Matplotlib; solver запускается из sparse finite-difference least squares. `--output` должен указывать на новую папку. Summary хранит каждый полный singular spectrum, weakest right modes, conditional covariance/correlation, validation controls и SHA-256 Jacobian/input/source hashes. Входы из `artifacts/calibration-diagnostic-v2` и capture baseline не изменяются; ни production calibration gate, ни server config не задействованы. Exact-truth residual sigma является численным полом синтетической генерации. Detector-RMS covariance предполагает iid scaling и не доказывает iid detector error; cluster sandwich имеет только 12 synthetic views. Не использовать эти local standard errors как target-platform confidence limits или порог допуска. Теория/результаты/границы: [[research/CALIBRATION_JOINT_INFORMATION_PROTOCOL]], [[validation/JOINT_CALIBRATION_INFORMATION]].

## Совместный ответ intrinsics и поз досок

Воспроизвести frozen component-attribution experience на сохранённых synthetic inputs:

```bash
python3 tools/configurator.py compare-attribution --output artifacts/calibration-attribution-repeat
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_attribution.py --results artifacts/calibration-attribution-repeat
python3 tests/test_joint_information.py
ctest --test-dir build -R joint_calibration_information --output-on-failure
```

Выполняются 12 exact joint baselines и 144 signed refits на `h=0.05 px`. Summary хранит успех/отказ каждой стороны пары, `J+ d` response, fitted intrinsics, pose responses, costs и hashes. Это synthetic diagnostic без production gate. Протокол и sparse результаты: [[research/CALIBRATION_COMPONENT_ATTRIBUTION_PROTOCOL]], [[validation/CALIBRATION_COMPONENT_ATTRIBUTION]].

Post-hoc dense LM follow-up на тех же входах запускается отдельно, чтобы отчёты не смешивали solver paths:

```bash
python3 tools/configurator.py compare-attribution --solver lm \
  --output artifacts/calibration-attribution-lm-repeat
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_attribution.py \
  --results artifacts/calibration-attribution-lm-repeat --suffix _lm
```

Без `--solver` используется sparse TRF/LSMR. Follow-up выбран после наблюдения failures и не является независимой solver comparison или production recommendation: [[validation/CALIBRATION_COMPONENT_SOLVER]].


## Переключение серверного fusion без перезапуска

На работающем сервере с capability `fusion_runtime_v1` через `svctl` (использует sv-client-lib):

```bash
build/svctl --unix /tmp/sv-prototype command fusion_catalog
build/svctl --unix /tmp/sv-prototype state
# Вставить фактическую config_revision из state вместо 0.
build/svctl --unix /tmp/sv-prototype command configure_fusion --params '{"base_config_revision":"0","fusion":{"mode":"angular_feather","diagnostic":"weights","edge_width_px":24,"angle_power":4}}'
```

Для удалённого сервера заменить `--unix /tmp/sv-prototype` на `--tcp HOST CONTROL_PORT DATA_PORT` с endpoint из конфигурации сервера. Текущий сервер обслуживает одну сессию: перед отдельным CLI-клиентом отключить simulator/другой клиент. Для каждого следующего изменения использовать новую `config_revision`; сохранить исходный `fusion` для восстановления. Настройки временные, но вне experiment lease persistent `apply_calibration` сохраняет весь текущий snapshot — сначала восстановить baseline. Подробности и ограничения: [[engineering/PROTOCOL_IMPLEMENTED#Переключение fusion во время работы: fusion_runtime_v1]].

Для парного исследования сравнивать кадры с совпадающими input IDs при paused replay и с revision из ACK. Отдельные CLI-подключения сами по себе ещё не создают воспроизводимый сценарий и не сбрасывают временную историю.


## Сценарное сравнение fusion через sv-client-lib

Первый C++ runner доступен через `svctl research SCENARIO_JSON REPORT_JSON`; тот же application service в `examples/common/research` предназначен для будущего GUI. Нужен сервер текущей версии с replay-источником; отключить остальные клиенты, поскольку сервер пока односессионный.

Подготовка минимальной записи и запуск сервера в первом терминале:

```bash
python3 tools/simulator.py --config configs/synthetic.json --output artifacts/runtime-screen/fixture --frames 12
SV_EGL_PLATFORM=surfaceless build/sv-server --config configs/synthetic.json --manifest artifacts/runtime-screen/fixture/manifest.json --ipc-dir /tmp/sv-runtime-screen --trace artifacts/runtime-screen/server.jsonl
```

Во втором терминале:

```bash
build/svctl --unix /tmp/sv-runtime-screen research configs/research/fusion-screen.json artifacts/runtime-screen/report.json
```

Для удалённого сервера заменить endpoint на `--tcp HOST CONTROL_PORT DATA_PORT`. Scenario/report находятся у клиента; камеры/manifest для replay находятся у сервера. Не передавать один путь для scenario и report. Exit 0 означает успешную серию и восстановление; exit 4 — failed/interrupted report, exit 3 — ошибка ввода/соединения/записи отчёта. При failed report проверить `restore_error`; при невозможности восстановления восстановить настройки вручную или перезапустить сервер с исходным конфигом.

Сценарий содержит `schema_version=1`, `seed`, `warmup` (0..100), `repeats` (1..1000), список `variants` (1..32) с fusion settings. Общий бюджет — не более 10000 кадров. Warmup задаёт число полных прогревочных блоков; каждый блок содержит все варианты в перемешанном порядке. Пример выполняет 2 прогревочных и 7 измерительных блоков для семи режимов, всего 63 кадра. Порядок воспроизводим в той же реализации стандартной библиотеки; фактический порядок всегда записан в samples, переносимость `std::shuffle` между STL не обещается.

Runner сохраняет исходные fusion/surface/pause, останавливает replay на текущем READY frame set, для каждого варианта применяет весь fusion и ждёт соответствующую state revision. Сверяются input IDs, frame-set ID и настройки. Отчёт включает исходное состояние, actual catalog/backend/server fingerprint, нормализованный scenario SHA-256, raw metadata/RGBA SHA-256 каждого кадра и p50/p95 стадий без warmup (nearest-rank percentile). После серии восстанавливаются fusion, surface, исходный RGB и pause/resume. Повторный replay не запускается: сравнение относится к текущему остановленному набору.

Это **screen механизма управления и warm-render стоимости**, а не финальное исследование качества/FPS: нет independent truth, dataset content registry, seek/reset истории или нового кадра на каждый sample. Задержка очереди/control и 16 ms limiter не включаются в `render_wall`; отсутствие захвата/decode/upload делает результаты несопоставимыми с sustained pipeline throughput. Прогрев чередующихся вариантов не измеряет cold-cache стоимость. Нужны последующие live/replay-sequence сценарии и независимые сцены.

Runner теперь требует `experiment_lease_v1`: получает lease на 30 s и продлевает перед каждым trial и cleanup. При cancel через C++ service либо исключении callback выполняются restore RGB и release; затем runner ждёт server state idle и проверяет fusion/pause. В report добавлены lease_id и final_state. Persistent calibration apply блокируется сервером во время lease.

Если CLI завершился через Ctrl+C/kill или control закрылся, сервер запускает восстановление fusion/pause; при потере сети без немедленного EOF срабатывает TTL. Клиентский JSON report после kill может остаться пустым — durable checkpoint ещё нет; результат recovery проверять новым `svctl state` и server trace. Пока restoring/failed сервер отвергает мутации; failed требует перезапуска. Recovery source ограничен 5 s при работающем server loop, но зависший GPU/драйвер невозможно прервать этим watchdog. Позиция replay и temporal history не восстанавливаются. Потеря только data при живом control обнаруживается для lease по отсутствию renew. Долгий callback не должен превышать TTL. Автоматические retries мутаций отключены.


### Носители и дополнительные алгоритмы

`fusion-screen.json` теперь содержит семь режимов, включая native distance/cut/multiband. Для гибридных методов output width × height должен быть ≤262144 (исходный synthetic 640×360 подходит). Если время CPU слишком велико, используйте отдельный config с меньшим output для всех сравниваемых методов; смешивать разрешения в одном рейтинге нельзя.

```bash
build/svctl --unix /tmp/sv-runtime-screen --timeout-ms 30000 research configs/research/carrier-screen.json artifacts/runtime-screen/carriers.json
```

Carrier screen выполняет пять вариантов с одним edge_feather: plane, bowl, dome+floor, cylinder+floor, cube+floor. Плотности сеток пока разные; это smoke механизма переключения, не итоговое сравнение качества при равных ресурсах. `variants[].surface` — optional полный объект config surface. Без него runner каждый раз возвращает исходную геометрию, включая после варианта с explicit surface. Вариант также принимает `pyramid_levels` и `smoothness_weight`.

Ручное управление (подставьте актуальную decimal-string revision из state; между разными CLI соединениями lease не сохраняется):

```bash
build/svctl --unix /tmp/sv-runtime-screen command fusion_catalog
build/svctl --unix /tmp/sv-runtime-screen command surface_catalog
build/svctl --unix /tmp/sv-runtime-screen state
build/svctl --unix /tmp/sv-runtime-screen command configure_fusion --params '{"base_config_revision":"0","fusion":{"mode":"graph_cut_multi_band","pyramid_levels":4,"smoothness_weight":0.1}}'
build/svctl --unix /tmp/sv-runtime-screen command configure_surface --params '{"base_config_revision":"1","surface":{"type":"dome_floor_v1","dome_radius_m":14,"dome_latitude_cells":24,"dome_longitude_cells":64,"floor_radial_cells":16}}'
```

Эти команды temporary и не записывают файл. Вне lease автоматического rollback нет: сохраните исходные fusion/surface, верните их с текущей revision либо перезапустите сервер с исходным config. Для подтверждающего опыта используйте runner, проверяющий restore. Semantics, GPU/CPU timings и проверка эталона: [[engineering/PROTOCOL_IMPLEMENTED]], [[validation/NATIVE_FUSION]].


### Проверка полного гибридного изображения

При BUILD_TESTING доступен `sv-render-fusion-probe`. Он выполняет тот же Renderer, что сервер, и сохраняет bounded projected layers, actual RGBA, fallback/ego и index.json. Это локальный проверочный executable, не remote debug API и не заменяет quality oracle. Проверка сервера по SV01 входит в CTest:

```bash
ctest --test-dir build -R 'render_fusion_parity|native_fusion_parity|analytic_reference' --output-on-failure
```

Для иллюстрации на Blender-записи создайте отдельный config с output 320×180 (исходный Blender config 960×540 превышает native pixel budget). Например, используйте `artifacts/native-fusion-runtime/server.json`, если сохранён smoke предыдущего этапа:

```bash
SV_EGL_PLATFORM=surfaceless build/sv-render-fusion-probe artifacts/native-fusion-runtime/server.json artifacts/blender-street/manifest.json artifacts/native-fusion-raster-real-v2
python3 docs/diploma/plot_native_fusion.py --capture artifacts/native-fusion-raster-real-v2
```

Probe читает только первый manifest frame, использует его четыре камеры для четырёх native методов и трёх diagnostics; результат не зависит от playback cursor сервера. Raw f32 слои имеют endian в index.json, изображения RGBA8 имеют top-left origin. Local artifacts не входят в Git; итоговый рисунок, его metadata и скрипт хранятся рядом с дипломом.

В offline `reference.render` используется серверное имя `pyramid_levels`; прежнее `num_pyramid_levels` остаётся alias, конфликтующие значения отклоняются. `smoothness_weight` передаётся в оба graph-cut метода. Новый 84-case screen:

```bash
python3 tools/run_e_stitch_01.py --config tests/data/e_stitch_01_v1/config.json --dataset tests/data/e_stitch_01_v1 --output artifacts/e-stitch-01-mask-v2
python3 docs/diploma/plot_e_stitch.py --summary artifacts/e-stitch-01-mask-v2/e_stitch_01_summary.json --output docs/diploma/figures/experiments/e_stitch_mask_v2.png
```

### Настройка алгоритмов из GUI без Python

```sh
cmake --build build --target sv-server sv-simulator
build/sv-server --config configs/synthetic.json --manifest artifacts/blender-street/manifest.json --ipc-dir /tmp/sv-prototype
# В другом терминале (endpoint берётся из server config):
build/examples/sv-simulator/sv-simulator --unix /tmp/sv-prototype
```

В примере используется заранее подготовленный Blender fixture `artifacts/blender-street/manifest.json` (подготовка описана выше); runtime сервера и GUI не запускает Blender/Python. Можно подключиться к уже работающему серверу с другим источником.

В верхней панели нажмите «Конфигурация». Вкладки «Сшивка», «Носитель» и «Операция» используют selectors, числовые поля и checkbox вместо ручного JSON. При подключении загружаются active/persisted/catalog; «Загрузить active в формы» явно заменяет черновик. Для плоскости задайте corner_height_m=0 у rectangular_bowl_v1. Семь алгоритмов и required geometry fields доступны, сервер проверяет view clearance и research pixel budget262144.

Нажмите «Проверить черновик», затем «Применить временно» либо подтвердите сохранение всего active snapshot и нажмите «Применить и сохранить на сервере». Применение выполняется через sv-client-lib; server config file меняет только сервер. После успешной операции GUI перечитывает актуальный snapshot. При stale revision обновите/загрузите active и перенесите правку вручную. Lost ACK блокирует новые изменения до operation status; same-ID retry явно использует исходный intent, новый instance требует ручной сверки. Подробности: [[engineering/CONFIG_SERVICE]].

Optional support/occlusion сохраняются для текущего типа носителя; при выборе другого типа сбрасываются с предупреждением. Optional reference/local support/AABB/uncertainty доступны во вложенных вкладках «Опора» и «Видимость». Формы остальных root groups и сценариев ещё предстоят. Editor не захватывает experiment lease; новый apply при lease будет отклонён. Перед запуском svctl отключите GUI: сервер пока односессионный.


### Исследование границ multiband

Сервер поддерживает `fusion.pyramid_boundary=zero|normalized` для `multi_band` и `graph_cut_multi_band`. Default zero сохраняет старые исследования; normalized — отдельный вариант с нормализацией Gaussian color mass по validity support. Каталог версии 3 перечисляет варианты. Получите текущую revision через `state`; в примере ниже замените `0` фактическим `config_revision`:

```sh
build/svctl --unix /tmp/sv-runtime-screen state
build/svctl --unix /tmp/sv-runtime-screen command configure_fusion --params '{"base_config_revision":"0","fusion":{"mode":"multi_band","pyramid_levels":4,"pyramid_boundary":"normalized"}}'
build/svctl --unix /tmp/sv-runtime-screen research configs/research/pyramid-boundary-screen.json artifacts/pyramid-boundary-report.json
```

Apply полностью заменяет fusion snapshot, остальные optional поля получают defaults. Конфигурация временная. Тот же выбор доступен в формах симулятора и `FusionSettings` клиентской библиотеки. Прежний сценарий сравнивает четыре варианта на одном READY paused frame set. Новый sequence режим описан ниже. При NO_INPUT и наличии capability `experiment_step_v1` runner пробует один защищённый step; если READY не получен, завершает опыт с ошибкой и выполняет cleanup. Численные контролируемые опыты, отрицательный результат и ограничения: [[validation/PYRAMID_BOUNDARY]].

### Многокадровый native эксперимент и RGBA capture

Сервер выполняет fusion; общий C++ runner управляет им через `sv-client-lib`. Для воспроизводимого двухкадрового примера используются проверенные входы из Git:

```sh
cmake --build build --target sv-server svctl
mkdir -p artifacts/boundary-example
cp tests/data/object_stitch_v1/config.json artifacts/boundary-example/config.json
SV_EGL_PLATFORM=surfaceless build/sv-server --config artifacts/boundary-example/config.json --manifest tests/data/object_stitch_v1/manifest.json --ipc-dir /tmp/sv-boundary-example --trace artifacts/boundary-example/trace.jsonl
```

В другом терминале, отключив GUI от односессионного сервера:

```sh
build/svctl --unix /tmp/sv-boundary-example research configs/research/boundary-sequence.json artifacts/boundary-example/report.json
python3 tools/research/server_boundary.py --fixture tests/data/object_stitch_v1 --report artifacts/boundary-example/report.json --output artifacts/boundary-example/audit.json
```

`frames` — число последовательных paused frame sets (1…256, default 1), `capture_frames` — запись measurement RGBA (default false). Общий бюджет frames × variants × (warmup + repeats) ограничен 10000. Несколько кадров требуют replay hello capability `experiment_step_v1`; старый сервер отклоняется до lease/mutation. Native CLI создаёт свежую папку `report.json.frames`; повторное использование существующей capture папки отклоняется до опыта, прежний report не перезаписывается. Для повторного опыта задайте новое имя отчёта.

Каждый файл — RGBA8, top-left origin, размеры/stride/pixel format и SHA-256 находятся в sample metadata. Warmup изображения не сохраняются. Report содержит frame_baselines, frame_index каждого sample, frame_summaries и pooled summaries. Для анализа пар используйте frame summaries: объединённая статистика разных кадров не является оценкой доверительного интервала по независимым сценам. Python audit только читает результаты и независимый truth; он не управляет сервером и не выполняет product rendering.

Runner делает один защищённый step, если начальная пауза застала stale/NO_INPUT; затем перед каждым следующим кадром восстанавливает исходные fusion/surface и делает step. Варианты одного кадра обязаны использовать одинаковые inputs/frame_set_id. Cancel и ошибка capture вызывают cleanup. `restored=true`, `restore_scope=fusion_surface_pause_only` подтверждают только fusion/surface/pause. `cursor_restored=false`: позиция replay и история не возвращаются. Для независимого повторного старта перезапустите сервер с исходным manifest. На конце записи без loop можно получить ошибку step; runner не подменяет её повтором прежнего кадра.

Дополнительная девятикадровая серия требует сохранённых `artifacts/object-stitch-motion-inputs` и `artifacts/object-stitch-motion-capture`; запустите сервер с config/manifest из первого каталога и используйте `configs/research/boundary-motion-sequence.json`. Для audit задайте `--fixture artifacts/object-stitch-motion-inputs --capture artifacts/object-stitch-motion-capture`. Полная matrix пяти carriers запускалась последовательно с отдельным server config/report для каждого; менять разрешение/virtual view перед данным audit нельзя. Результаты и ограничения: [[validation/SERVER_BOUNDARY]].

### Сравнение binary и multilabel seam solvers

Используйте тот же tracked fixture/server из многокадрового примера, свежий report path и каталог config рядом с отчётом:

```sh
build/svctl --unix /tmp/sv-boundary-example research configs/research/multilabel-sequence.json artifacts/boundary-example/seam-report.json
python3 tools/research/server_boundary.py --fixture tests/data/object_stitch_v1 --report artifacts/boundary-example/seam-report.json --comparison seam_solver --output artifacts/boundary-example/seam-audit.json
```

Сценарий запускает оба graph-cut режима × binary_pairs/alpha_expansion на двух кадрах, zero boundary, два warmup/семь measured blocks. Alpha — дорогой CPU research path; первый опыт на Linux/Mesa добавляет сотни миллисекунд, поэтому его нельзя считать realtime конфигурацией автомобиля. Через typed library задайте `FusionSettings::seam_solver="alpha_expansion"`; через simulator загрузите actual fusion snapshot и добавьте seam_solver. GUI пока использует прежний JSON adapter; удобный selector/form остаётся в TODO. Обычный multi_band не использует seam solver.

Новый сервер объявляет catalog v4. Default бинарный field в snapshot может отсутствовать, это не потеря конфигурации. Неизвестные solver names отклоняются сервером; omission в full configure_fusion возвращает binary_pairs. Energy/convergence отражаются в frame.seam_optimization, raw reports и independent audit. Данные, параметры и ограничения: [[validation/MULTILABEL_SEAM]].

### Новые procedural seam cases

Генерация трёх frozen сцен и конвертация описаны в [[engineering/BLENDER#Восстановление новых frozen seam scenes]]. После получения inputs запускайте native сервер с **actual** config/manifest каждого seed. Для seed101:

```sh
mkdir -p artifacts/seam-generalization-v1/seed101-run/dome_floor
cp artifacts/seam-generalization-v1/seed101-inputs/config.json artifacts/seam-generalization-v1/seed101-run/dome_floor/config.json
SV_EGL_PLATFORM=surfaceless build/sv-server --config artifacts/seam-generalization-v1/seed101-run/dome_floor/config.json --manifest artifacts/seam-generalization-v1/seed101-inputs/manifest.json --ipc-dir /tmp/sv-seam101 --trace artifacts/seam-generalization-v1/seed101-run/dome_floor/trace.jsonl
```

Другой терминал:

```sh
build/svctl --unix /tmp/sv-seam101 --timeout-ms 10000 research configs/research/multilabel-sequence.json artifacts/seam-generalization-v1/seed101-run/dome_floor/report.json
```

Остановите сервер штатно и повторите для102/103 с новыми путями/socket directories. Не совмещайте timing trials со сборкой, CTest или Blender renders. После всех трёх записей независимый audit проверяет frozen plan, generator/capture hashes, poses/timestamps, full matrix, actual settings/optimization diagnostics и последний baseline restore:

```sh
python3 tools/research/server_boundary.py --study-plan assets/scenarios/seam-generalization-v1.json --study-root artifacts/seam-generalization-v1 --output artifacts/seam-generalization-v1/audit.json
```

Audit только читает данные, не запускает продукты. При новой генерации используйте новый study root и пути reports/captures; пример root исходного опыта приведён для объяснения структуры. Неполная трёхcase серия не объявляется завершённой. Физические camera tests и независимые типы окружения этим протоколом не заменены.

### Сравнение носителей в общем бюджете

Протокол: [[research/CARRIER_BUDGET_PROTOCOL]], результаты: [[validation/CARRIER_BUDGET]]. Нужны captured/converted seeds101–103 из предыдущего раздела; Python ниже только создаёт исследовательские config copies и provenance. Рендеринг/сшивку и orchestration trials выполняют C++ server и `svctl research`.

Подготовить все пятнадцать configs и заморозить hashes до запуска:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json
root = Path('artifacts/carrier-budget-v1')
plan_path = Path('configs/research/carrier-budget-plan.json')
plan = json.loads(plan_path.read_text())
root.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
(root/'provenance.json').write_text(json.dumps(dict(
    plan_sha256=sha(plan_path), inputs_plan_sha256=sha(plan['inputs_plan']),
    scenario_sha256=sha(plan['scenario'])), indent=2)+'\n')
for seed in plan['seeds']:
    for carrier in plan['carriers']:
        out = root/f'seed{seed}'/carrier['id']
        out.mkdir(parents=True, exist_ok=True)
        cfg = json.loads(Path(f'artifacts/seam-generalization-v1/seed{seed}-inputs/config.json').read_text())
        cfg['surface'] = carrier['surface']
        (out/'config.json').write_text(json.dumps(cfg, indent=2)+'\n')
PY
```

Пример одной пары seed/carrier, первый терминал:

```sh
SV_EGL_PLATFORM=surfaceless __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json build/sv-server --config artifacts/carrier-budget-v1/seed101/plane/config.json --manifest artifacts/seam-generalization-v1/seed101-inputs/manifest.json --ipc-dir /tmp/sv-budget-example --trace artifacts/carrier-budget-v1/seed101/plane/trace.jsonl
```

Во втором терминале:

```sh
build/svctl --unix /tmp/sv-budget-example --timeout-ms 10000 research configs/research/boundary-sequence.json artifacts/carrier-budget-v1/seed101/plane/report.json
```

Завершить первый server через Ctrl+C, повторить с новым process для каждой комбинации seeds101/102/103 и plane/bowl/dome_floor/cylinder_floor/cube_floor (соответствующие пути в frozen plan). Не менять surface внутри одного process этой серии: retained carrier buffers нарушат условие active=resident. Не выполнять одновременно сборку, CTest или Blender rendering. При ошибке сохранить неудачный report отдельно, а не смешивать его с успешной полной серией. Для другой машины путь Mesa vendor JSON может отсутствовать; выбранный EGL backend указать в отчёте и не смешивать fingerprints/platforms.

Когда готовы все пятнадцать reports:

```sh
python3 tools/research/server_boundary.py --budget-plan configs/research/carrier-budget-plan.json --budget-root artifacts/carrier-budget-v1 --inputs-root artifacts/seam-generalization-v1 --output artifacts/carrier-budget-v1/audit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/carrier-budget-v1/audit.json --captures-root artifacts/carrier-budget-v1 --fixture artifacts/seam-generalization-v1/seed101-inputs --capture artifacts/seam-generalization-v1/seed101-capture
```

Аудитор требует все120 условий, actual metadata/budget consistency, frozen profiles/recipes/hashes и успешный settings restore. Сравнение ограничено mesh position/index payload; оно не уравнивает full memory, raster work или spatial density и не ранжирует скорость носителей. Для изменения бюджета/геометрии создать новую версию plan/protocol/output, а не перезаписывать опубликованный baseline.

### Чувствительность к плотности сетки

Frozen plan `configs/research/carrier-refinement-plan.json`, протокол [[research/CARRIER_REFINEMENT_PROTOCOL]], результаты [[validation/CARRIER_REFINEMENT]]. Medium использует прежние raw reports/RGBA в `artifacts/carrier-budget-v1`, проверяемые по закреплённому SHA256 baseline. Coarse/fine создаются отдельно; физические размеры и inputs не менять.

Подготовка новых derived plans/configs/provenance (без обработки видеокадров):

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json
root = Path('artifacts/carrier-refinement-v1')
path = Path('configs/research/carrier-refinement-plan.json')
master = json.loads(path.read_text())
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
root.mkdir(parents=True, exist_ok=True)
(root/'provenance.json').write_text(json.dumps(dict(master_plan_sha256=sha(path)), indent=2)+'\n')
for level in master['levels']:
    if 'reused_root' in level:
        continue
    out = root/level['id']
    out.mkdir(exist_ok=True)
    plan = level['plan']
    derived = out/'plan.json'
    derived.write_text(json.dumps(plan, indent=2)+'\n')
    (out/'provenance.json').write_text(json.dumps(dict(plan_sha256=sha(derived),
        inputs_plan_sha256=sha(plan['inputs_plan']), scenario_sha256=sha(plan['scenario'])), indent=2)+'\n')
    for seed in plan['seeds']:
        for carrier in plan['carriers']:
            run = out/f'seed{seed}'/carrier['id']
            run.mkdir(parents=True, exist_ok=True)
            cfg = json.loads(Path(f'artifacts/seam-generalization-v1/seed{seed}-inputs/config.json').read_text())
            cfg['surface'] = carrier['surface']
            (run/'config.json').write_text(json.dumps(cfg, indent=2)+'\n')
PY
```

Запустить fresh server и native runner для каждой комбинации coarse/fine × seeds101/102/103 × пять carriers, тем же способом, что в предыдущем разделе. Например, первая комбинация, терминал1:

```sh
SV_EGL_PLATFORM=surfaceless __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json build/sv-server --config artifacts/carrier-refinement-v1/coarse/seed101/plane/config.json --manifest artifacts/seam-generalization-v1/seed101-inputs/manifest.json --ipc-dir /tmp/sv-refinement-example --trace artifacts/carrier-refinement-v1/coarse/seed101/plane/trace.jsonl
```

Терминал2:

```sh
build/svctl --unix /tmp/sv-refinement-example --timeout-ms 10000 research configs/research/boundary-sequence.json artifacts/carrier-refinement-v1/coarse/seed101/plane/report.json
```

Завершить сервер через Ctrl+C, затем перейти к следующей комбинации в порядке master plan. Не выполнять build/CTest/Blender capture параллельно. При наличии сохранённых medium originals и всех30 новых reports:

```sh
python3 tools/research/server_boundary.py --refinement-plan configs/research/carrier-refinement-plan.json --refinement-root artifacts/carrier-refinement-v1 --inputs-root artifacts/seam-generalization-v1 --output artifacts/carrier-refinement-v1/audit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/carrier-refinement-v1/audit.json --captures-root artifacts/carrier-refinement-v1 --fixture artifacts/seam-generalization-v1/seed101-inputs --capture artifacts/seam-generalization-v1/seed101-capture
```

Аудитор требует360 условий с120 pinned medium reuse, совпадающий native fingerprint, hashes, shape invariance, строго возрастающие cells/resources и корректный restore. Если medium originals отсутствуют или требуется новая сборка, создать новую версию полной серии/plan с новым закреплённым medium baseline: текущий протокол не допускает незаметной подмены прежних samples. Compact tracked audit сохраняет metrics/hash references; large raw reports/RGBA нужны для re-audit и не дублируются в Git. Fine output — finite reference для sensitivity, direct Blender RGB/object-ID остаётся независимым truth. Универсальный convergence/acceptance threshold и speed ranking из этой серии не следуют.

### Раздельные scene ROI и полоса границ

Это read-only анализ уже сохранённых360 conditions refinement, без запуска server/Blender. Нужны raw reports/RGBA coarse/fine и прежний medium, captured/converted seeds101–103, а также неизменный pinned `carrier_refinement_v1.json`. Frozen policy: `configs/research/spatial-roi-plan.json`, протокол [[research/SPATIAL_ROI_PROTOCOL]].

```sh
python3 tools/research/server_boundary.py --spatial-plan configs/research/spatial-roi-plan.json --refinement-root artifacts/carrier-refinement-v1 --inputs-root artifacts/seam-generalization-v1 --output artifacts/carrier-refinement-v1/spatial-audit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/carrier-refinement-v1/spatial-audit.json --inputs-root artifacts/seam-generalization-v1
```

Audit перепроверяет inputs/poses/calibration/visibility/reports/RGBA и совпадение прежних metrics, создаёт disjoint ground/other_scene/coded_target interior/boundary masks по direct object IDs и подтверждает pixel-weighted reconstruction прежней MAE. Empty ROI даёт null, не0. Ground —semantic road/markings, other_scene —остальные non-ego objects, не измеренный physical-height class. Полоса границ повторяет прежнее исключение из global metric и теперь измеряется отдельно. Новые masks не заменяют исторический baseline, не являются ghost correspondence или carrier-shell coverage. Результаты/ограничения: [[validation/SPATIAL_ROI]]. Для новых inputs/policy подготовить отдельную версию plan/analysis; текущий набор уже просмотрен и остаётся exploratory.

### Native диагностика покрытия носителя

Исследовательская сборка с `SV_GPU=ON` и `BUILD_TESTING=ON` предоставляет C++ probe над тем же Renderer, который использует сервер:

```sh
cmake --build build --target sv-carrier-probe -j4
LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless build/sv-carrier-probe artifacts/carrier-budget-v1/seed101/dome_floor/config.json artifacts/carrier-single
```

На выходе `carrier-regions.u8` (top-left, один ID на pixel) и `report.json` с layout, pixel counters, mesh resources, config/mask SHA256 и source fingerprint. Input camera frames не нужны. Дополнительный pass выполняется только при запросе локального `RenderInspection`; SV01 API его пока не экспортирует. Значения ID/ограничения и вся15-case matrix: [[research/CARRIER_COVERAGE_PROTOCOL]], [[validation/CARRIER_COVERAGE]]. Geometry coverage не заменяет camera validity, quality truth или triangle-hit coverage; wall-clock render с inspection не использовать для обычных performance trials.

### Сшивка в боковом ракурсе

Protocol [[research/CARRIER_LATERAL_PROTOCOL]], results [[validation/CARRIER_LATERAL]]. Нужны новый complete capture по [[engineering/BLENDER#Боковой ракурс с независимым truth]] и сохранённые historical captures/inputs `artifacts/seam-generalization-v1`: audit проверяет побитовое равенство камерных RGB, калибровки и траектории. Сборка: SV_GPU=ON, BUILD_TESTING=ON, binaries sv-server/svctl/sv-carrier-probe.

```sh
cmake --build build --target sv-server svctl sv-carrier-probe -j4
python3 tools/research/carrier_lateral.py --plan configs/research/carrier-lateral-plan.json --root artifacts/carrier-lateral-repeat --inputs artifacts/carrier-lateral-repeat-inputs --run-binaries build --output artifacts/carrier-lateral-repeat/audit.json
```

Root native matrix обязан быть новым. Python wrapper запускает15 fresh native processes, сохраняет command lines/environment/binary hashes/logs, проверяет readiness с deadline, выполняет C++ research scenario и завершает server в finally; сам fusion не реализует. Native geometry probe выполняется отдельно от timing samples. При сбое root сохраняется для диагностики: для повтора используйте другой root. Не запускайте сборку/Blender/CTest параллельно с timing samples. Unix-сокеты должны быть разрешены средой запуска.

Без `--run-binaries` выполняется read-only audit всех120 quality conditions: capture/config/view/helper hashes, input equivalence, mesh ceilings, RGBA repeatability/restore, region-ID masks и раздельные common floor/shell/other metrics. Полная команда для сохранённой текущей серии:

```sh
python3 tools/research/carrier_lateral.py --plan configs/research/carrier-lateral-plan.json --root artifacts/carrier-lateral-v1 --inputs artifacts/carrier-lateral-inputs-v1 --output artifacts/carrier-lateral-v1/audit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/carrier-lateral-v1/audit.json --captures-root artifacts/carrier-lateral-v1 --inputs-root artifacts/carrier-lateral-inputs-v1
```

Фигуры показывают все шесть кадров и полный набор profiles; качество сравнивается на одинаковом support внутри каждого кадра. Это exploratory paused CPU/Mesa screen, не sustained FPS и не target validation. Native региональные masks пока не экспортируются как SV01 subscription.

## Управляемая высота и параллакс

Сначала получить complete Blender capture по [[engineering/BLENDER#Управляемая высота и расстояние объекта]], затем выполнить из корня проекта:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-height-plan.json --root artifacts/parallax-height-repeat --inputs artifacts/parallax-height-repeat-inputs --run-binaries build --output artifacts/parallax-height-repeat/audit.json
```

Нужны native GPU build с testing (`sv-server`, `svctl`, `sv-carrier-probe`), Python NumPy/SciPy/OpenCV/Pillow для независимого аудита и конвертации. Launcher создаёт30 fresh servers через Unix IPC, запускает native `svctl research`, сохраняет исходные RGBA, reports, logs и конфигурации; алгоритмы остаются в C++. Для timing остановите Blender render и другие нагрузки. Каталог --root должен быть новым. Повторная read-only проверка не запускает серверы:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-height-plan.json --root artifacts/parallax-height-repeat --inputs artifacts/parallax-height-repeat-inputs --output artifacts/parallax-height-repeat/reaudit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/parallax-height-repeat/reaudit.json --captures-root artifacts/parallax-height-repeat --inputs-root artifacts/parallax-height-repeat-inputs
```

Сохранённый checked-in baseline: [[validation/PARALLAX_HEIGHT]]. Python здесь является research harness/oracle, не продуктовым API или реализацией сшивки.

### Оценка только наблюдаемой поверхности объекта

Для тех же native результатов используйте `--visibility-baseline` вместо новых запусков:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-height-plan.json --root artifacts/parallax-height-v1 --inputs artifacts/parallax-height-inputs-v1 --visibility-baseline docs/validation/baselines/parallax_height_v1.json --output artifacts/parallax-height-v1/visibility-audit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/parallax-height-v1/visibility-audit.json --captures-root artifacts/parallax-height-v1 --inputs-root artifacts/parallax-height-inputs-v1
```

Сначала проверяются все прежние числовые результаты и data hashes; только code digests расширенных analyzer/object_metrics.py сохраняются как новые. Historical baseline не перезаписывается. Conditional report сравнивается с `docs/validation/baselines/parallax_visibility_v1.json`; `--run-binaries` в этом режиме запрещён. Метрика и ограничения: [[validation/PARALLAX_VISIBILITY]]. Новых Blender captures не требуется; fully framed targets остаются отдельным следующим опытом.

## Полностью помещающийся в кадр объект и два монтажа

Сначала получить полный новый capture по [[engineering/BLENDER#Полный объект в кадре: два варианта монтажа]], затем из корня проекта:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-framed-plan.json --root artifacts/parallax-framed-repeat --inputs artifacts/parallax-framed-repeat-inputs --run-binaries build --output artifacts/parallax-framed-repeat/audit.json
```

Matrix12 cases×5 carriers запускает60 fresh production servers через native svctl/sv-client-lib. Нужны GPU+testing build, Python scientific dependencies и Blender для inputs. Остановите capture/другие нагрузки перед timing. Actual perturbed calibration экспортируется; это не оценка calibration solver. Root должен быть новым. Повторная read-only проверка и рисунки:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-framed-plan.json --root artifacts/parallax-framed-repeat --inputs artifacts/parallax-framed-repeat-inputs --output artifacts/parallax-framed-repeat/reaudit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/parallax-framed-repeat/reaudit.json --captures-root artifacts/parallax-framed-repeat --inputs-root artifacts/parallax-framed-repeat-inputs
```

Source hashes, config/trajectory equality внутри rig, intrinsics/non-camera settings между rigs, full-box margin≥8px и direct-ID border проверяются audit. Conditional score=null при пустом visibility support. Не заменяйте null нулём и не подбирайте view после сравнения quality. Протокол/результаты: [[research/PARALLAX_FRAMED_PROTOCOL]], [[validation/PARALLAX_FRAMED]].

В этой проверенной серии GLVND выбрал NVIDIA RTX5070Ti даже при LIBGL_ALWAYS_SOFTWARE=1. Для паспорта опыта используйте `render_device` нового audit и `catalog.gl_vendor/gl_renderer` native reports, а не environment flag. При сравнении платформ фиксируйте CPU fusion и GLES renderer отдельно; frozen CPU-Mesa wording уточнена в [[validation/PARALLAX_FRAMED]].

## Парный контроль разрешения cubemap

Сначала выполнить новый Blender capture по [[engineering/BLENDER#Парный контроль разрешения cubemap]]. Затем запустить native серию (новый root, без параллельного Blender/сборки/тестов):

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-sampling-plan.json --root artifacts/parallax-sampling-v1 --inputs artifacts/parallax-sampling-inputs-v1 --run-binaries build --output artifacts/parallax-sampling-v1/summary.json
```

Отдельная read-only команда перепроверяет обе серии, воспроизводит прежний baseline и создаёт все 480 пар. Требуются сохранённые raw данные обеих серий; одних JSON недостаточно:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-sampling-plan.json --root artifacts/parallax-sampling-v1 --inputs artifacts/parallax-sampling-inputs-v1 --sampling-baseline docs/validation/baselines/parallax_framed_v1.json --reference-root artifacts/parallax-framed-v2 --reference-inputs artifacts/parallax-framed-inputs-v2 --output artifacts/parallax-sampling-v1/paired.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/parallax-sampling-v1/paired.json --captures-root artifacts/parallax-sampling-v1 --inputs-root artifacts/parallax-sampling-inputs-v1 --reference-root artifacts/parallax-framed-v2
```

Проверяются фактические размеры всех cubemap faces, неизменные 400×400 fisheye inputs, direct truth и native binaries/device. Разности — 512 минус 256; `null` не заменяется нулём. Это sampling control на одном flat-world family, а не подтверждение пригодности на Авроре или сходимости по всем разрешениям: [[research/PARALLAX_SAMPLING_PROTOCOL]].

## Чувствительность к выходному разрешению

После [[engineering/BLENDER#Независимый truth640×360]] запустить новую серию, сохраняя512px cubemap/400px fisheye:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-output-plan.json --root artifacts/parallax-output-v1 --inputs artifacts/parallax-output-inputs-v1 --run-binaries build --output artifacts/parallax-output-v1/summary.json
python3 tools/research/parallax_height.py --plan configs/research/parallax-output-plan.json --root artifacts/parallax-output-v1 --inputs artifacts/parallax-output-inputs-v1 --output-resolution-pair --output artifacts/parallax-output-v1/paired.json
```

Вторая команда read-only проверяет frozen reference из runtime plan (прежние512px sampling captures/native/full summary с SHA256). Все reference raw данные должны оставаться на диске; изменившийся reference summary отклоняется. Первый root должен отсутствовать. Нужны GPU/testing build и scientific Python dependencies. Во время native trials остановить Blender/сборки/тесты/plots.

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/parallax-output-v1/paired.json --captures-root artifacts/parallax-output-v1 --inputs-root artifacts/parallax-output-inputs-v1 --reference-root artifacts/parallax-sampling-v1
```

Изображения справа640×360 получены сервером; low output увеличивается nearest-neighbor только для display difference, не для scores/truth. Centroid сравнивается как distance/output-height, predicted area как доля кадра. Report содержит high8px и high32px component policies;32px эквивалентны прежним8px по площади. Null сохраняются. Между captures сравниваются decoded cube/fisheye RGB и статические PNG metadata; файловые hashes проверяются внутри каждой серии (Blender меняет Date/Camera/Scene/RenderTime). Протокол и явное уточнение приёмки: [[research/PARALLAX_OUTPUT_PROTOCOL]], [[validation/PARALLAX_OUTPUT]].

## Чувствительность к разрешению fisheye

После [[engineering/BLENDER#Fisheye800 при прежнем поле зрения]] выполните новую серию. Для полного парного аудита сохраните raw outputs и inputs предыдущего output-resolution опыта; их пути и summary SHA256 закреплены в `configs/research/parallax-sensor-plan.json`.

```bash
python3 tools/research/parallax_height.py --plan configs/research/parallax-sensor-plan.json --root artifacts/parallax-sensor-v1 --inputs artifacts/parallax-sensor-inputs-v1 --run-binaries build --output artifacts/parallax-sensor-v1/summary.json
python3 tools/research/parallax_height.py --plan configs/research/parallax-sensor-plan.json --root artifacts/parallax-sensor-v1 --inputs artifacts/parallax-sensor-inputs-v1 --sensor-resolution-pair --output artifacts/parallax-sensor-v1/paired.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/parallax-sensor-v1/paired.json --captures-root artifacts/parallax-sensor-v1 --inputs-root artifacts/parallax-sensor-inputs-v1 --reference-root artifacts/parallax-output-v1
```

Первая команда требует нового native root; для повторного аудита уже завершённой серии исключите `--run-binaries`. При повторном capture/trial используйте новые каталоги и новый runtime plan с соответствующим reference. Не запускайте Blender/build/tests/plots одновременно с измерениями.

Сервер выполняет весь rendering/fusion. Python здесь проверяет эксперимент и строит независимые метрики/иллюстрации. Audit требует одинаковые binaries/device, decoded cube RGB/static metadata, independent direct RGB/ID/visibility и позы. Меняются только camera resolution400→800 и pixel-center intrinsics. Выход640×360 и angular bound прежние; `null` сохраняется. Параметры source-edge остаются в pixels: это full-pipeline sensitivity, а не чистая ablation фильтра. [[research/PARALLAX_SENSOR_PROTOCOL]].

## Фактические координаты носителя

Сборка диагностического native probe и GTest:

```bash
cmake -S . -B build -DSV_GPU=ON -DBUILD_TESTING=ON -DSV_GTEST_TESTS=ON
cmake --build build -j4
ctest --test-dir build -R 'carrier_inspection|render_modes|render_fusion_parity' --output-on-failure
EGL_PLATFORM=surfaceless build/sv-carrier-probe configs/synthetic.json artifacts/carrier-positions-demo --positions
```

Используйте новый каталог для каждого сохранённого опыта. Probe читает конфигурацию и рендерит геометрию без input frames. Файл `carrier-positions.f32` имеет shape(H,W,3), little-endian float32, порядок X/Y/Z, метры в системе автомобиля, top-left raster. `report.json` содержит SHA256, dimensions, payload size и scope. Background/ego обозначены NaN, а не нулевой точкой автомобиля; сверяйте также `carrier-regions.u8`. Footprint region3 имеет позицию carrier mesh и не означает наличие camera texture. Без `--positions` сохраняется обычный region-only export.

Для явного выбора двух backends на Linux:

```bash
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json EGL_PLATFORM=surfaceless build/sv-carrier-inspection-tests --gtest_output=xml:artifacts/carrier-tests/nvidia.xml
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless build/sv-carrier-inspection-tests --gtest_output=xml:artifacts/carrier-tests/mesa.xml
```

Пути vendor JSON зависят от установленного GLVND. NVIDIA требует доступа к GPU; integration parity требует bind Unix IPC. Ошибки EGL initialization/bind в ограниченной песочнице требуют запуска с соответствующим доступом. Читайте фактический device из результатов: одна `LIBGL_ALWAYS_SOFTWARE` не фиксирует vendor.

Render-thread API для native потребителя:

```cpp
sv::RenderInspection inspection;
sv::RenderInspectionRequest request;
request.carrier_positions = true;
auto frame = renderer.render(frames, view, &inspection, request);
// inspection.carrier_positions: CV_32FC3; three extra diagnostic passes are opt-in.
```

Данные владеют памятью через OpenCV. Диагностика выполняется после обычного frame capture и его stage timers; три дополнительных passes/readbacks имеют overhead, который пока не опубликован отдельно и должен учитываться при будущем wire workflow. На этом этапе API доступен внутри renderer и через локальный probe; клиентская команда/канал будут следующим расширением sv-server/sv-client-lib.

Восстановление рисунков §4.59 при сохранённых native exports:

```bash
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_carrier_positions.py --report docs/validation/baselines/carrier_positions_v1.json --root artifacts/carrier-inspection-v1
```

Полный read-only повтор аудита сохранённого опыта:

```bash
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_carrier_positions.py --root artifacts/carrier-inspection-v1 --output-report artifacts/carrier-inspection-v1/reaudit.json
cmp docs/validation/baselines/carrier_positions_v1.json artifacts/carrier-inspection-v1/reaudit.json
```

Для него нужны frozen reference reports/RGBA/configs `parallax-sensor-*`, архивы executables/XML/test-provenance, неизменённые code/plan hashes. При новом capture/build/backend создавайте новую версию опыта; не подменяйте historical baseline. Условия и пределы пилота: [[validation/CARRIER_POSITIONS]]; ближайший дальнейший этап: [[planning/ROADMAP#Ближайший исследовательский этап: source correspondence и triangle-hit]].

### Проверка координат исходных камер

Для разработчика: `RenderInspectionRequest::source_projections=true` включает локальный capture четырёх source pixel/status matrices. Проверка: `ctest --test-dir build -R '^(carrier_inspection|render_fusion_parity)$' --output-on-failure` после сборки `sv-carrier-inspection-tests`, `sv-render-fusion-probe`, `sv-server`. Нужны GLES и доступ к Unix socket. Форматы, коды отказа и команды выбора Mesa/NVIDIA: [[engineering/RENDERING#Opt-in source projections]], [[validation/CARRIER_POSITIONS#Расширение: координаты исходных камер]]. Пока это внутренний C++ API и тестовый export; CLI команды удалённого capture нет.

### Проверка вклада камер

Разработчик может передать `RenderInspectionRequest{true, true, true}` для одновременного carrier/source/hybrid-fusion capture; результат находится в `RenderInspection::fusion`. Поддерживаемые режимы, матрицы и стоимость: [[engineering/RENDERING#Opt-in fusion contributions]]. Проверка C++ core: `build/sv-fusion-boundary-tests`; полная команда сборки и native/wire parity: [[validation/CARRIER_POSITIONS#Расширение: вклад камер после смешивания]]. Это внутренний API; клиентская команда capture пока отсутствует.

### Проверка соответствия source sample реальной поверхности Blender

Соберите `cmake --build build --target sv-correspondence-probe -j4`. Test binary принимает `CONFIG MANIFEST OUTPUT_DIRECTORY [FRAME_INDEX]` (default0), проверяет индекс и читает выбранный набор кадров и пишет ordinary RGBA/actual carrier XYZ/source UV/status с SHA256 и переносимым little-endian layout. Для повторения исторического опыта и readonly audit используйте [[validation/SOURCE_CORRESPONDENCE#Проверки и воспроизведение]]. Независимый Blender ray oracle запускается отдельно и восстанавливает ранее активную сцену. Это исследовательский инструмент; remote capture через sv-client-lib ещё предстоит добавить.

Height/second-frame diagnostic capture и read-only audit описаны в [[validation/SOURCE_CORRESPONDENCE_CONTROLS#Воспроизведение]]. Это20 frozen edge-feather conditions; новый capture выполняйте с копией плана и новым output_root. При движении Blender world hits нужно преобразовывать в vehicle frame для camera projection, native carrier points — в world для surface comparison. Тест rotation+translation закрепляет это правило.

Isolated planar geometry control и воспроизведение: [[validation/PLANAR_CORRESPONDENCE#Воспроизведение и аудит]]. `plot_planar_correspondence.py` читает сохранённые native exports и Blender rays, формирует отчёт и графики; он не запускает продуктовый renderer. Для нового capture копируйте план с новым output_root. Summary содержит measured failures: `exact_planar_controls_passed=false` в опубликованном baseline, даже если audit command завершился успешно.

Paired precision/backend control и read-only audit: [[validation/PLANAR_PRECISION#Воспроизведение]]. Используется прежний архивный `sv-correspondence-probe`, fixed shader hashes, configs/inputs и Mesa environment; новые сборки требуют отдельной provenance. `plot_planar_precision.py` строит исследовательский отчёт, не заменяет C++ Renderer.

Displaced target ray/RGB audit: [[validation/DISPLACED_CORRESPONDENCE#Воспроизведение]]. `plot_displaced_correspondence.py` читает прежние native exports и новые Blender hits. Native pipeline не заменяется Python; отчёт разделяет point-ray support, RGB proxy и color reconstruction. Новый scene capture выполняйте с копией плана и новым output_root.

Matched hybrid diagnostic capture: `build/sv-render-fusion-probe CONFIG MANIFEST OUTPUT [FRAME_INDEX] [--color-only]`. Default — прежние18 variants первого frame; color-only —6. Index содержит input/config/manifest hashes, effective settings и product hashes/bytes; native band files — `levelN-cameraC-band.f32`. Полная процедура60 conditions и read-only audit: [[validation/HYBRID_CORRESPONDENCE#Воспроизведение]]. Диагностический capture не является wire subscription и не должен включаться в обычные performance timings.

Read-only four-texel audit и изображения: `MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_source_footprint.py --output-report artifacts/source-footprint-v1/reaudit.json`; сравнить с `docs/validation/baselines/source_footprint_v1.json`. Предпосылки, Blender replay и ограничения: [[validation/SOURCE_FOOTPRINT#Воспроизведение]].

Read-only cube footprint audit: `MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_cube_footprint.py --output-report artifacts/cube-footprint-v1/reaudit.json`; сравнить с `docs/validation/baselines/cube_footprint_v1.json`. Frozen requests, Blender replay и ограничения: [[validation/CUBE_FOOTPRINT#Воспроизведение и проверки]].

Read-only новых hybrid domains: `MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_hybrid_domain.py --output-report artifacts/hybrid-domain-v1-final/reaudit.json`; сравнить с `docs/validation/baselines/hybrid_domain_v1.json`. Inputs/Blender replay и precision control: [[validation/HYBRID_DOMAIN#Воспроизведение]].

Заданная высота rectangular carrier доступна через существующий `sv-client-lib::Client::configure_surface`: добавить `reference_height_m` к полной surface object и отправить с актуальной revision. Диапазон/default доступны в `surface_catalog.rectangular_reference_height`. Это temporary исследовательская настройка, не автоматически оценённая глубина. Read-only опыт: `MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_reference_height.py --output-report artifacts/reference-height-v1/reaudit.json`. Сборка,8 native captures, provenance и ограничения: [[validation/REFERENCE_HEIGHT#Воспроизведение]]. Frozen historical audits выполнять на соответствующей версии source hashes в отдельном checkout; после изменения renderer не отключать hash checks.

Для локального height control в полной `surface` передать через `Client::configure_surface` вложенный объект:

```json
"local_support_patch": {
  "center_x_m": 2.4, "center_y_m": 2.3,
  "half_length_m": 0.2, "half_width_m": 0.2,
  "height_m": 0.8, "transition_m": 0.2
}
```

Координаты относятся к автомобилю: при движении rig неподвижный мировой объект требует пересчёта центра. Поля и ограничения доступны в `surface_catalog.rectangular_local_support`. Это временная настройка геометрии с заранее известным положением/высотой; библиотека не оценивает их по камерам. Удалить участок — отправить полную surface без этого объекта. Геометрия дороги вне участка сохраняется, но результат RGB и видимость нужно оценивать отдельно. Формула и ограничения mesh: [[engineering/RENDERING]].

Парный local-height опыт, native команды, сохранённые результаты и ограничения: [[validation/LOCAL_SUPPORT#Проверки и воспроизведение]]. Рисунки: `MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_local_support.py --output-report artifacts/local-support-v1/reaudit.json`. Требуются frozen raw artifacts; Python здесь выполняет только independent audit/plot.

Для supplied source-visibility control добавить в полную rectangular surface:

```json
"source_occlusion_box": {
  "min_m": [2.2, 2.1, 0.6],
  "max_m": [2.6, 2.5, 1.0],
  "tolerance_m": 0.02
}
```

Применить через `Client::configure_surface` с актуальной revision; допустимые поля доступны в `surface_catalog.rectangular_source_occlusion`. Границы — vehicle-frame XYZ, поэтому при движении автомобиля их нужно пересчитать. Настройка исключает перекрытые источники перед слиянием; сам объём не рисуется, RGB depth estimation не выполняется. Удалить control — отправить полную surface без `source_occlusion_box`. Не подменять этим заранее известным объёмом решение задачи оценки глубины реальной сцены.

Native commands и provenance supplied visibility опыта: [[validation/SOURCE_VISIBILITY#Проверки и воспроизведение]]. Independent audit/plot: `MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_source_visibility.py --output-report artifacts/source-visibility-v1/reaudit.json`. Требуются archived raw inputs/products и соответствующие source hashes.

Read-only residual audit прежних source-visibility outputs: `MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_visibility_residual.py --output-report artifacts/visibility-residual-v1/reaudit.json`. Нужны archived scene-ray arrays и pinned native/input files. Blender capture-команда с явным preload research module, выборка и ограничения: [[validation/VISIBILITY_RESIDUAL#Воспроизведение и проверки]]. Это диагностический audit/plot, нового режима сервера он не добавляет.

Экспериментальный footprint-aware вариант supplied-box control:

```json
"source_occlusion_box": {
  "min_m": [2.2, 2.1, 0.6], "max_m": [2.6, 2.5, 1.0],
  "tolerance_m": 0.02, "mode": "bilinear_radial_v1", "guard_texels": 0
}
```

Через `Client::configure_surface` можно переключать mode и guard без перезапуска. Guard0 проверяет четыре положительных bilinear texels; guard1 расширяет проверку до4×4 соседей. Для возврата к прежнему predicate передать mode=center,guard_texels=0 либо опустить оба поля. Ненулевая дисторсия камер для этого исследовательского варианта запрещена. Это conservative radial-range approximation, которая может обрезать полезную границу; качество требует парного сравнения, а не оценки только исчезнувших ghosts.

Экспериментальный `bilinear_radial_v1` guard0/1 прошёл oracle/parity checks, но потерял большинство видимых target pixels в16-case pilot. Для обычной работы сохранён default `center`; результаты и команды независимого audit: [[validation/FILTER_VISIBILITY]].

## Supplied box surface preservation (experimental)

`surface.source_occlusion_box.mode="bilinear_box_surface_v1"` adds a bounded alternative to `bilinear_radial_v1`. When uncertainty is omitted, after the unchanged central occlusion test, a carrier endpoint within the configured support band (default10µm, L∞ distance) of any finite supplied-box face retains the central sample. Central occlusion rejection remains in force; optional facing filtering below further restricts the surface exemption. Points outside that surface band use the prior radial footprint test with `guard_texels=0/1`; zero-distortion equidistant optics are required. Interior points and face extensions farther than the numerical band receive no exemption. This recognizes one known box, not arbitrary scene surfaces or RGB depth. Default remains `center`.

The mode is selected through existing `Client::configure_surface` with the whole surface and current `base_config_revision`; the server validates and publishes it between renders. `surface_catalog.rectangular_source_occlusion.modes` lists it, and `default_support_tolerance_m=0.00001` and `support_tolerance_range_m=[0,0.05]` describe the optional configurable `support_tolerance_m`. It is separate from configurable `tolerance_m` (central endpoint bias); clients must not submit the catalog metadata as config fields. No new message/transport is introduced. A GUI control for this mode is still pending.

24-condition box-surface pilot улучшает target preservation до75/76 и69/69 при outside1/0, но strict gate первого кадра не пройден; default center сохранён. Сравнение и воспроизведение: [[validation/BOX_SURFACE_VISIBILITY]].

`source_occlusion_box.support_tolerance_m` is optional and valid only with `mode=bilinear_box_surface_v1`: finite value in[0,0.05]m, default0.00001m. It changes support membership alone; it does not expand the occluder, alter `tolerance_m`, remove central-ray rejection or change neighbor inset. A larger band explicitly assumes more geometry uncertainty and can exempt incorrect surfaces. The old fixed-band catalog key is replaced by the default/range keys. Omitted field preserves previous pixels; other modes reject the field to avoid silent no-op configuration. Apply and restore via existing full-surface `Client::configure_surface` and revision checks.

76-condition sensitivity и команды повторения: [[validation/VISIBILITY_SENSITIVITY]]. Nominal support_tolerance_m=0.001/tolerance_m=0.02/guard1 сохраняет76/76 и69/69, но robustness к±5mm Z bounds error не подтверждена. Это developmental profile, default center не изменён.


## Диагностика локальной поддержки через сервер

После подготовки и запуска фотографической улицы командами выше можно получить маску из того же renderer через C++ клиентскую библиотеку:

```sh
build/sv-client-probe --unix /tmp/sv-street --frames 1 --support
# Для сервера с явно настроенными TCP listeners:
build/sv-client-probe --tcp 127.0.0.1 47000 47001 --frames 1 --support
```

probe — пример/проверочный consumer sv-client-lib: запрашивает catalog, выбирает render_support_v1, release-ит прежние in-flight кадры и выводит JSON headers с eligible_pixels/positive_local_weight_pixels. Это counts по raster, не число независимо наблюдаемых scene points. В текущем профиле одна активная control/data pair; использовать probe как текущий клиент сервера. Требуется color diagnostic и idle experiment lease. После отключения probe новое подключение снова получает final_frame. GUI selector/palette пока не добавлены.

В собственном клиенте после ready и проверки HELLO capabilities:

```cpp
client.output_catalog();
const auto command_id = client.configure_output(sv::client::OutputProduct::RenderSupport);
// Wait for accepted ACK and then matching product/state_revision; release old frames.
// For a delivered owning shared_ptr<const sv::Message> named message:
sv::client::RenderSupportView mask(message);
const auto pixel = mask.at(0, 0); // bounds checked; counts/path/geometry
client.release(mask.message().header);
// mask keeps the immutable payload alive even after release.
// To return to color on the same connection:
client.configure_output(sv::client::OutputProduct::FinalFrame);
```

Integer RGBA channels нельзя напрямую показывать как фотографию. Использовать палитру по path/counts, отдельно выделять ego/background. Маска не является confidence, scene visibility или полным multiband contribution. Формат, ограничения и handling ошибок: [[engineering/PROTOCOL_IMPLEMENTED#Выбор data product: output_selection_v1 / render_support_v1]], проверки: [[validation/OUTPUT_PRODUCTS]].

## Bounded central uncertainty (experimental)

`surface.source_occlusion_box.uncertainty` is optional. If present, both `radius_m` (finite[0,0.05]m) and `ambiguous_policy` (`accept` or `reject`) are required; extra keys are rejected. Omit the object for nominal behavior; radius0 retains historical classification. For fixed carrier/poses/endpoint bias, the eroded supplied box certifies blocking and the dilated box certifies clearance. Accept admits ambiguous central rays to the existing nominal footprint stage; reject returns native `SourceProjectionStatus::uncertain_occluder=8`. Status1 remains policy eligibility, including accepted ambiguity, not definitely-clear confidence. Status7 can represent definite central blocking or nominal footprint rejection; rejected UV stays NaN. Support membership and neighbor rays remain nominal and independent of central uncertainty.

The existing full-surface `Client::configure_surface` call applies/restores these fields with current revision/lease; the server advertises required fields/range/policies under `surface_catalog.rectangular_source_occlusion.uncertainty`. No new wire command, persistence or parallel subscription is implied. Accepted ambiguity and rejection reasons are now remotely selectable through source_visibility_v1; the legacy UV/status inspection remains unchanged: [[validation/SOURCE_VISIBILITY_PRODUCT]]. GUI forms remain pending. Defaults are unchanged. Implementation,54 native RGBA controls, backend provenance and negative robustness results: [[validation/NATIVE_UNCERTAINTY]].

Добавьте к существующему `source_occlusion_box` в полной surface:

```json
"uncertainty": {"radius_m": 0.005, "ambiguous_policy": "accept"}
```

Сначала запросите `state` и `surface_catalog`; используйте актуальную `config_revision`. В callback получения state ACK пример изменения через библиотеку выглядит так (обработчик ACK/rejection остаётся у приложения):

```cpp
auto surface = state_ack.at("surface").as_object();
auto &box = surface.at("source_occlusion_box").as_object();
box["uncertainty"] = boost::json::object{{"radius_m", 0.005},
                                         {"ambiguous_policy", "accept"}};
const auto revision = sv::parse_decimal_u64(
    std::string(state_ack.at("config_revision").as_string()));
const auto command_id = client.configure_surface(revision, std::move(surface));
```

Этот пример предполагает уже заданный box и отсутствие чужого experiment lease. При owned lease передайте его ID третьим аргументом. Для `reject` измените policy; для возврата nominal удалите `uncertainty` из полного surface и примените с новой revision. Дождитесь accepted ACK и нужной revision кадра; stale/rejected command не применять локально. Настройка temporary, после restart требуется повторное применение. Native capture/reaudit команды — [[validation/NATIVE_UNCERTAINTY#Проверки и воспроизведение]].

## Ориентация конечных граней

При существующем supplied box можно включить дополнительный experimental filter. В обработчике state ACK используйте полный текущий surface и актуальную revision:

```cpp
auto surface = state_ack.at("surface").as_object();
auto &box = surface.at("source_occlusion_box").as_object();
box["mode"] = "bilinear_box_surface_v1";
box["require_facing_surface"] = true;
const auto revision = sv::parse_decimal_u64(
    std::string(state_ack.at("config_revision").as_string()));
const auto command_id = client.configure_surface(revision, std::move(surface));
```

Сохраните прежний full surface для restore; не подставляйте experimental bounds/radius из исследования в произвольную сцену. Пример предполагает valid существующий box, zero-distortion equidistant cameras и отсутствие чужого lease; при собственном lease передайте ID третьим аргументом. Дождитесь accepted ACK и новой config revision кадра. `false`/omission возвращает прежнюю facing-disabled обработку; поле недопустимо в других modes. Server catalog сообщает default и scope. Изменение временное, без сохранения на диск. Central policies остаются независимыми: их acceptance не обходит facing check. GUI form пока отсутствует; причина9 доставляется через source_visibility_v1. Условия, результаты и отрицательные robustness cases: [[validation/NATIVE_FACING]].

## Причины отказа и принятая неоднозначность

Проверьте capability source_visibility_v1 и output_catalog. При attached data, color view и idle lease:

```cpp
client.output_catalog();
const auto command_id = client.configure_output(sv::OutputProduct::SourceVisibility);
```

В обработчике Frame event с owning event.message, после accepted ACK и проверки state_revision/product:

```cpp
if (sv::client::validate_output_product(*event.message) == sv::OutputProduct::SourceVisibility)
{
    const sv::client::SourceVisibilityView view(event.message);
    const auto status = view.at(100, 50, 3); // x,y,camera; предварительно проверить размеры
    const bool eligible = sv::source_visibility_eligible(status);
    // Сохранить owning view при необходимости; payload не является display RGBA.
    client.release(view.message().header);
}
```

Для прежнего цвета вызовите configure_output(sv::OutputProduct::FinalFrame). In-flight old product тоже release-ить, затем ожидать нужный product/revision; reconnect сбрасывает selector, библиотека автоматически его не повторяет. A byte — camera3 status, не opacity; используйте отдельную palette. Status10 accepted ambiguous и11 accepted clear доступны только для explicit positive-radius uncertainty;1 без такого certificate. Принятие не гарантирует положительный вклад fusion или правильную поверхность сцены. Полный status table, сценарий, tests и ограничения: [[validation/SOURCE_VISIBILITY_PRODUCT]]. GUI palette/camera selector пока не реализованы; API доступен любому C++ client через installed headers.

## Показать участки fallback

В sv-simulator откройте панель «Алгоритмы и геометрия сервера», нажмите «Обновить состояние и catalog», затем **«Показать fallback штриховкой»**. Повторное нажатие возвращает обычное заполнение. Кнопка сохраняет остальные fusion settings и работает с локальным/удалённым сервером через прежнее подключение. Требуется объявленная capability fallback_display (catalog v5). Настройка временная, без записи файла; shader выполняется на сервере.

Для C++ клиента задать `settings.fallback_display.mode="hatch"` перед `configure_fusion(revision, settings)`. Scenario variant принимает `"fallback_display":{"mode":"hatch"}`. Configure полностью заменяет fusion: укажите остальные нужные параметры, omission возвращает defaults. Сетка отмечает только фактический pipeline fallback; она не обнаруживает все неверные источники или поверхности. [[validation/FALLBACK_DISPLAY]].

## Измерить RGB-разногласие камер

Через `sv-client-lib` проверьте output_catalog и выберите `OutputProduct::SourceDisagreement` методом configure_output. Сервер отправит source_disagreement_v1 вместо final_frame; следующий выбор FinalFrame или reconnect вернёт обычный кадр. Нужны color view и idle experiment lease. В owning SourceDisagreementView используйте defined перед интерпретацией encoded_rgb_range: одна камера не даёт pair comparison. Число eligible sources включает нулевые fusion weights. Диагностика доступна по прежним Unix/TCP connections; отдельная GUI palette/selector пока не добавлена.

Проверка серверной доставки: `ctest --test-dir build -R output_products --output-on-failure` с рабочим EGL backend. Frozen study/plots и обнаруженный drift двух исторических pairings описаны в [[validation/SOURCE_DISAGREEMENT]]; исследовательский threshold не выбран и автоматически sources не отбрасываются.

## Повторить matched Mesa source-truth опыт

`python3 tools/research/mesa_source_truth.py --output-report /tmp/mesa-source-truth-repeat.json` проверяет сохранённые compiled captures по frozen plan. `--capture` предназначен только для первого создания output directory; нужны archived correspondence binary, source images и проверенный Blender geometry export. Основная метрика остаётся в серверном source_disagreement_v1, Python выполняет independent evaluation/plots. Полные команды, hashes, ограничения и текущие counterexamples: [[validation/MESA_SOURCE_TRUTH]]. Новый build/backend требует отдельного versioned plan/report, а не подмены frozen hashes.

## Повторить baseline новых текстурированных сцен

Blender fixture и independent validation: [[validation/TEXTURED_SOURCE]]. После первого native capture
повторный collector запускается командой `python3 tools/research/textured_source.py --output-report /tmp/textured-source-repeat.json`.
Основной renderer/metric выполняются compiled C++/GLSL probes; Python используется для независимой
проверки и figures, не как продуктовый camera source. Для первого запуска нужны Blender export,
frozen binaries и EGL/Mesa; существующие output directories не перезаписываются.

## Прочитать активную и сохранённую конфигурацию

```sh
build/svctl --unix /tmp/sv-prototype command config_read
build/svctl --tcp 127.0.0.1 53101 53102 command config_read
```

Запрос не требует JSON-ввода и не изменяет конфиг. ACK configuration содержит coherent
active/persisted/revision snapshot ConfigStore, без session view и launch-only overrides.
Нужен сервер с capability config_read_v1. Подробный C++ API и ограничения: [[engineering/CONFIG_SERVICE]].

## Получить допустимые параметры и проверить изменения

```sh
build/svctl --unix /tmp/sv-prototype command config_catalog
# Revision брать из config_read; 0 здесь только пример:
build/svctl --unix /tmp/sv-prototype command config_validate --params '{"base_config_revision":"0","changes":{"fusion":{"mode":"multi_band","pyramid_boundary":"normalized"}}}'
```

Validation ничего не применяет и не сохраняет. Полная замена fusion требует mode; omitted
fusion fields получают defaults. Surface требует все geometry fields. Остальные root groups
пока не поддержаны. Для программного клиента используйте typed API, для GUI формы будут
добавлены следующим шагом. Контракт и ограничения: [[engineering/CONFIG_SERVICE]].

## Применение конфигурации сервером

После read_configuration()/configuration_catalog() доступны validate_configuration(base, changes) и apply_configuration(base, changes, persist). Fusion/surface передаются полными replacement groups. persist=false оставляет файл неизменным; persist=true сохраняет весь active snapshot. Ответ разбирается parse_configuration_application(); не повторять mutation после потери ACK автоматически. GUI binding fusion/surface реализован; для других групп работа продолжается; команды CLI, диаграмма, ограничения lease и проверки: [[engineering/CONFIG_SERVICE]].

## Результат операции после потери ACK

Для новых apply использовать explicit apply_configuration_once(instance, operation_id, base, changes, persist) и configuration_operation_status(instance, operation_id), затем parse_configuration_operation(). ID/intent хранит приложение; библиотека не повторяет mutation автоматически. Журнал сохраняется при reconnect, ограничен128 операциями и теряется при restart; старый instance нельзя автоматически заменять новым. Команды CLI, sequence diagram и ограничения: [[engineering/CONFIG_OPERATIONS]].

## ConfigService в ноутбучном GUI

В sv-simulator нажмите «Конфигурация» в верхней панели. Вкладки «Сшивка», «Носитель», «Операция» используют кнопки, selectors и числовые поля. Проверка и explicit runtime/persistent apply выполняются сервером через библиотеку; для save требуется checkbox полного active snapshot. После потери ACK сначала status/reconciliation, без автоматического replay. Подробные шаги, рисунок, optional-field ограничения и проверки: [[engineering/CONFIG_SERVICE#GUI sv-simulator: формы и восстановление результата]]. Сборка laptop GUI: SV_CLIENT=ON/SV_SIMULATOR=ON; touch-only: SV_SIMULATOR=OFF.

В «Носитель → Опора/Видимость» можно включить и отключить reference height, local support patch, AABB и uncertainty без JSON. Выключение удаляет optional поле; смена visibility mode очищает несовместимые guard/facing/support параметры. Bounds/units читаются из catalog, сервер проверяет cross-field constraints и optics. Пошаговый контракт, рисунок и проверки: [[engineering/CONFIG_SERVICE#Расширения геометрии: Опора и Видимость]]. После обновления бинарника перезапустите GUI; старый процесс продолжает использовать прежние QML ресурсы.

## Devel telemetry: выключенный по умолчанию сбор

Группа telemetry, CLI hot enable/read/disable, typed C++ API, cursor reset/loss и единицы:
[[engineering/TELEMETRY#Использование]]. Read не включает сбор; при disabled history пуста.
Клиенту всё ещё нужны control/data; GUI и svctl не могут одновременно владеть сервером.

В sv-simulator серверный сбор включается через Конфигурация → Телеметрия → validate/apply.
Чтение включается отдельно в панели Телеметрия сервера; CPU/Память/Render находятся в её вкладках.
Полный workflow и трактовка квантилей: [[engineering/TELEMETRY#sv-simulator: настройки и графики]].

Сохранение и offline-просмотр: **Сохранить окно в отчёт** → выбрать JSON; для **Открыть отчёт offline**
остановить reader и отключиться от сервера. Файл содержит до120 raw samples, не весь experiment
trace. Подробности и ограничения: [[engineering/TELEMETRY_ARCHIVE]].

В telemetry **Этапы** выбрать существующий span/input age; **I/O** — source queue, delivery flags,
record age или collection wall. Пояснения под selector различают CPU wall/GPU query/возраст,
а stage quantiles считают только fresh records. Backend counters и ограничения:
[[engineering/TELEMETRY#Pipeline и source/delivery графики]]. Работает и для сохранённого window.

При включённом telemetry collection camera/replay source измеряет успешные input operations.
В I/O появились12 per-camera choices read/load/prepare; подходящие значения зависят от source kind.
Для проверки epoch guard на replay: pause → изменить telemetry period/history → apply → step.
Стартующий по умолчанию looping replay может успеть повторно использовать cache до первого render;
cache_hit=true не доказывает, что cold load не выполнялся. [[engineering/SOURCE_TIMING]].


В telemetry → I/O выберите «Камера N: Ошибки чтения», «Переподключения» либо «Worker остановлен». Это cumulative source counters/flag, не rate/latency. Без camera backend значения отсутствуют. Diagnostics доступны даже без новых входных кадров; подробности и exact counts находятся под графиком и в сохранённом окне. [[engineering/SOURCES#Диагностика состояния каждой аппаратной камеры]].


### Профили аппаратного захвата

Для четырёх OpenCV/V4L2 камер необязательная source.capture задаёт FPS, FOURCC и buffer_count
по camera_id. Полный пример и limits: [[engineering/SOURCES#Настройки и проверка согласования OpenCV/V4L2]].
Размер в cameras[id].resolution должен соответствовать реальной калибровке; пример
configs/v4l2-camera.json содержит синтетическую калибровку и не является готовым аппаратным rig.

```sh
cmake --build build -j4
build/sv-server --config configs/v4l2-camera.json --ipc-dir /tmp/sv-v4l2
build/examples/sv-simulator/sv-simulator
```

Во второй программе подключитесь к Unix endpoint /tmp/sv-v4l2. Если драйвер не подтвердил
явно заданную настройку, смотрите source_status camera_configuration_rejected в журнале сервера:
исправьте профиль, сохраните config и перезапустите сервер. Открытие выполняется заново после
retry; source profile пока не меняется через ConfigService. Если FPS/FOURCC/buffers не требуется
фиксировать, опустите соответствующее поле. Requested FPS/readback не заменяют измерения
реального межкадрового интервала.

MOCK-проверки без устройств:

```sh
build/sv-source-work-tests --gtest_filter='CameraNegotiation.*:WidthHeightConversionFpsFourccBuffers/CameraNegotiation.*:CameraCaptureConfig.*'
```

MOCK моделируют negotiation и lifecycle; подключение реальных камер этим не проверяется.


В telemetry → I/O смотрите Checked mode в сведениях камеры: разрешение, checked FPS/FOURCC/buffers и open_attempt. «Не проверен» означает, что optional свойство не запрашивалось; «недоступен» — capture закрыт/ещё не согласован/не поддерживает этот snapshot. Сравнивайте с startup source.capture и calibrated resolution; FPS readback не равен измеренному cadence. Снимок сохраняется в окне отчёта. [[engineering/SOURCES#Проверенный режим камеры через SDK]].


В I/O details сравнивайте Current checked mode камеры со строкой Selected camera: после reconnect используемый кадр может иметь прежний capture open_attempt/FPS. Это не ошибка SDK. Для camera metadata результата используйте parse_frame_camera_inputs(); он не валидирует pixel payload. Запись Selected сохраняется в offline report. [[engineering/SOURCES#Происхождение выбранного кадра при reconnect]].


### Повторное подключение камер

Для недоступных устройств можно увеличить задержки и включить capped backoff в
source.capture[camera_id].recovery; если нужен конечный лимит, задайте max_consecutive_failures.
Полный fragment, bounds и semantics: [[engineering/SOURCES#Политика повторов и backoff аппаратной камеры]].
Default unlimited сохраняет прежнее поведение. После изменения startup config перезапустите
сервер. Если worker исчерпал лимит, I/O Worker stopped будет1, а в журнале появится
camera_retry_exhausted; resume не перезапускает его. Изменение max_delay не задаёт timeout read.

```sh
cmake --build build -j4
build/sv-source-work-tests --gtest_filter='CameraRecoveryPolicy.*:OpenReadFormatException/CameraRecoveryBudget.*'
```

Это MOCK/контрактные проверки recovery, без физических камер. Длительное ожидание retry
прерывается stop; зависший вызов драйвера по-прежнему может задерживать shutdown.


Для диагностики конечного retry budget откройте I/O details в sv-simulator: `Recovery` показывает streak, полный интервал текущего ожидания и terminal reason. `retry_exhausted` требует перезапуска источника/сервера; Resume worker не пересоздаёт. `release_failure` сообщает об ошибке close; `stop_requested` — о штатном запросе stop после успешного close. В экспортированной telemetry эти поля находятся в `samples[].source.camera_diagnostics[].recovery`. Архив содержит исторические snapshots, а не полную трассу. [[engineering/SOURCES#Снимок восстановления камеры]].


Если optional FPS/FOURCC/buffers в source.capture не заданы, смотрите отдельную строку Automatic readback в I/O details. Она показывает наблюдаемый драйверный режим, а «неизвестно» означает unavailable get; это не measured sensor FPS и не строго проверенный запрос. Current и Selected mode могут относиться к разным открытиям. Эти поля сохраняются в telemetry report под negotiated_mode.automatic_properties. [[engineering/SOURCES#Наблюдаемые свойства автоматического профиля камеры]].


На Boost≥1.83 продуктовые JSON-пути используют общую Precise number policy: SV01 decoder, load_config, GUI/CLI intents и archive import. В installed SDK доступен sv/json.hpp. Framing/команды не меняются; Legacy dataset reader и старый Boost имеют отдельные ограничения. Новые config/wire baseline могут отличаться последними binary64 bits. Контракт, regression и миграция: [[engineering/JSON_PRECISION]].


Если тестовый Aurora SDK не предоставляет opencv-devel/glm-devel, доступен offline bundle профиль
обоих RPM spec. Он использует заранее собранные target .so и заголовки; внутри SDK приложение
ничего не скачивает. Подготовка Source1/Source2, CMake/RPM commands и target проверки:
[[engineering/PRIVATE_DEPENDENCIES]]. Команды запуска установленных sv-server/sv-client не меняются;
приватный OpenCV ищется по package RPATH.

### Матрица параметров общего C++ runner

Сервер должен быть запущен с replay dataset, поддерживающим последовательное получение кадров.
Готовый пример `configs/research/multiband-matrix.json` разворачивает4 variants,3 frame sets,
2 warmup и5 measured blocks:84 trials,60 измеряемых RGBA captures.

```sh
mkdir -p artifacts/multiband-matrix-run
build/svctl --unix /tmp/sv-prototype --timeout-ms 10000 research \
  configs/research/multiband-matrix.json artifacts/multiband-matrix-run/report.json
```

Путь отчёта и `report.json.frames` должны быть новыми. Даже metadata-only запуск больше не
перезаписывает существующий report. Для следующего запуска выбирайте новую папку/имя.
В report проверяйте success/restored, scenario_sha256, trial_order_algorithm, trial_plan_sha256,
trial_plan и trial_index samples. Matrix разворачивается в report.scenario.variants; seed задаёт
порядок, не мир. Продолжение после аварии, возврат cursor и GPU/OS cache reset пока не реализованы.
Полный контракт: [[architecture/RESEARCH_RUNTIME#Ограниченные матрицы и фиксированный план испытаний]].


### Повторить сценарий с начала replay

В scenario JSON добавьте `"reset_replay": true`; пример
`configs/research/multiband-matrix.json` уже включает этот параметр. Используйте прежнюю команду
`svctl research` и новый report path для каждого прогона. Обновлённые сервер/библиотека нужны
совместно: требуется HELLO capability `replay_reset_v1`.

Runner получает lease, сервер очищает свой decode cache и sync history, загружает первую строку
manifest и остаётся на паузе. Последующие frame sets идут через step. Проверяйте
`ready_preparation`, `frame_baselines[*].metadata.inputs[*].source_timestamp_ns`, success/restored
и hashes. При неполной строке0 runner завершает опыт ошибкой; строка не пропускается. Старое поведение — `reset_replay` отсутствует/false, старт с текущего кадра.
Нельзя считать reset очисткой GPU/OS caches либо возвратом прежней позиции после завершения.
Семантика команды: [[PROTOCOL_IMPLEMENTED#Сброс replay к первой строке (`replay_reset_v1`)]].


### Оценить RGB результата по внешнему эталону без Python

В scenario укажите `reset_replay: true` и `quality_reference: "quality/reference.json"`.
Подготовьте P6 RGB8 truth и binary RGB mask для каждого frame set; manifest включает dimensions,
полный view (включая FOV/clip/target), четыре input guards, kind и dataset_id.
Запуск остаётся `svctl research scenario.json new-report.json`; loader/metrics — общий C++ service.
Полный пример, команды, формулы и ограничения: [[engineering/RESEARCH_QUALITY]].

Report сохраняет reference/pixel hashes, per-sample masked MAE/RMSE/PSNR и summaries по frame,
исключая warmup и не размножая observations числом paused repeats. Отсутствие guard match
завершает опыт с cleanup. success означает workflow, не пройденный quality threshold.
Серверный capture помечайте kind=regression; он не заменяет независимый Blender/direct-view truth.


### Проверить заранее заданные пороги качества

В manifest quality_reference добавьте непустой criteria, например
`{"max_mae_rgb8":5.0,"max_rmse_rgb8":10.0,"min_psnr_db":30.0}`. Это пример синтаксиса,
научные пороги нужно обосновать отдельно. Они хэшируются до подключения и применяются ко всем
measured samples; warmup исключён. Runner сохраняет всю серию даже при нарушении порогов.
Читайте report.quality_acceptance и sample.quality.acceptance: workflow success не означает
качество passed. CLI0 — выполнено и passed/not_requested;5 — выполнено, качество failed;
4 — ошибка/incomplete. Полный формат: [[engineering/RESEARCH_QUALITY#Заданные до запуска критерии качества]].
Для новых серий используйте reference schema2 с decoded-image и numeric camera-model guards.

### Новая сцена cargo yard и матрица пяти носителей

Генерация/экспорт/просмотр через sv-server и sv-simulator:
[[engineering/BLENDER#Независимые семейства окружения и раздельные seeds]].
Recipes — assets/scenarios/quality-families-v1.json; native20-variant matrix —
configs/research/family-carrier-screen.json. Все fusion/carrier переключения, provenance checks,
RGBA capture, metrics и restore выполняет общий C++ runner через sv-client-lib.
Python fixture harness остаётся средством подготовки/независимой проверки truth, не алгоритмом
рендеринга. GUI управления несколькими исследованиями и compiled multi-dataset supervisor ещё
не реализованы. Результаты64-sample smoke и640-trial matched screen:
[[validation/FAMILY_CARRIER_SCREEN]]. True camera calibration в этом пилоте передана серверу;
автоматическая calibration jobs проверяется отдельно.

### Несколько исследований одной записи

```sh
build/svctl --unix /tmp/sv-prototype --timeout-ms 5000 \
  research-batch configs/research/batch-repeatability.json artifacts/batch-repeatability-01
```

Нужен запущенный replay server с минимум тремя кадрами; output directory должен быть новым.
План повторяет multiband matrix и проверяет воспроизводимость, не независимость сцен.
Сценарии/эталоны и все output paths проверяются до подключения. Отчёты — в `batch.json`
и отдельных `ID.json`; exit 5 означает завершённую серию с непройденным quality threshold,
exit 4 — техническую неполноту. Подробный формат, API и границы: [[engineering/RESEARCH_BATCH]].

### Визуальный план исследования

В sv-simulator откройте **Исследования** в шапке окна. На вкладке **План** выберите методы,
frames/warmup/repeats/seed, pyramid boundary/levels и seam solver; задайте новый output directory.
**Начать исследование** запускает общий C++ FileExperiment вне UI thread. Доступны progress,
отмена с restore и таблица timings. Ручной JSON не требуется, сформированный сценарий сохраняется.
В группе «Внешний эталон» выберите reference manifest для MAE/RMSE/PSNR;
либо выберите «Запустить сохранённый сценарий» для импорта с его relative reference. Критерии
отображаются отдельно от workflow success; отсутствие метрики не означает нулевую ошибку. Пошаговая инструкция: [[engineering/RESEARCH_BATCH#Исследование из sv-simulator]].


Для серии в том же окне выберите **Источник плана → Запустить batch-план** и
`configs/research/batch-repeatability.json`, укажите новый output directory.
Сервер должен уже воспроизводить нужную запись (пример использует первые три frame sets).
GUI показывает номер сценария, его прогресс, отмену и итоговые статусы каждого участника;
полные отчёты сохраняются в `batch.json` и `ID.json`. Провал качества продолжает серию,
техническая ошибка останавливает её. Подробности: [[engineering/RESEARCH_BATCH#Запуск batch-плана из GUI]].


Чтобы собрать новую серию без JSON, выберите **Источник плана → Составить серию**, добавьте
сценарии файловым диалогом и задайте порядок кнопками ↑/↓. Можно повторить один scenario
несколько раз; предел8, у всех `reset_replay=true` и одна текущая replay-запись сервера.
**Начать исследование** фиксирует состав и после проверки всех входов сохраняет
`OUTPUT/inputs/plan.json`. Этот файл затем можно выбрать как batch-план или передать
`svctl research-batch`. Исходные scenario/reference/data должны оставаться доступными.
Подробности: [[engineering/RESEARCH_BATCH#Визуальное составление серии]].


Для правки существующей серии используйте **Составить серию → Импортировать план…**.
Выберите batch JSON (также подходит сохранённый `OUTPUT/inputs/plan.json`), затем меняйте
порядок/состав кнопками. Успешный импорт заменяет список целиком, ошибка или отмена сохраняет
черновик. Импорт не запускает серверные операции; полный preflight выполняется по **Начать
исследование**. Подробности: [[engineering/RESEARCH_BATCH#Импорт плана в редактор серии]].


Чтобы сохранить подготовленную серию без сервера, нажмите **Составить серию → Сохранить
черновик как…** и выберите новое имя JSON файла в существующем каталоге. Записывается schema1
plan с абсолютными scenario paths; сами datasets не копируются и не проверяются. Такой файл
можно снова импортировать или исполнить через GUI/`svctl research-batch`. Существующие файлы
защищены от замены. Изменённый после сохранения состав отмечается в интерфейсе.
Контракт и IO ограничения: [[engineering/RESEARCH_BATCH#Сохранение черновика без исполнения]].


## Сравнение сохранённых исследовательских отчётов

В `sv-simulator` откройте **Исследование → Сравнение**, выберите два individual
`report.json` либо batch `ID.json` и нажмите **Сравнить отчёты**. Сервер не требуется.
Показаны полные настройки, два паспорта и допустимые разницы B−A с отдельными gates
времени и качества. Кнопка **Сохранить сравнение…** создаёт новый audit JSON с raw hashes.
Форматы, предел16MiB, отмена и ограничения интерпретации: [[engineering/RESEARCH_REPORTS]].

## Доступные алгоритмы в форме исследования

После подключения sv-simulator дождитесь загрузки конфигурации и каталога.
В **Исследование → План → Настроить через форму** снимите методы с пометкой
«недоступен», выберите объявленные boundary/solver и число уровней в показанном диапазоне.
Сообщение над формой объясняет, почему запуск заблокирован. Изменение каталога не заменяет
выбранный метод или число уровней автоматически. Для batch/import действуют прежние
проверки общего runner. Подробности: [[engineering/RESEARCH_BATCH#Каталог возможностей в форме исследования]].

## Пороговые критерии в исследовании

Для наследования criteria эталона оставьте новый блок **Критерии качества опыта** выключенным.
Для другого протокола опыта выберите внешний reference, включите **Задать полный набор
критериев для этого опыта**, отметьте нужные MAE/RMSE/PSNR и введите пределы.
Набор заменяет manifest criteria целиком; исходный файл не редактируется. Пустой выбор и
criteria без reference блокируют запуск. Параметры фиксируются в scenario/report.
Диапазоны, точность формы и metadata источника порогов: [[engineering/RESEARCH_QUALITY#Критерии конкретного сценария без изменения эталона]].

## Сравнение параметров fusion через форму

В блоке **Матрица параметров** включите диапазон уровней и задайте от/до/шаг либо
сравнение обоих решателей шва. Для одной комбинации оставьте переключатели выключенными.
C++ сводка ниже показывает фактическое число variants/trials с учётом выбранных методов;
оси неиспользуемых параметров не размножают опыт. Исправьте ошибки диапазона/каталога
до запуска. Готовый `scenario.json` содержит все variants и повторяется через `svctl research`.
Примеры и ограничения: [[engineering/RESEARCH_BATCH#Матрица параметров fusion в форме]].


Для сравнения носителей без ручного JSON используйте в исследовании sv-simulator
«Набор носителей: снимки active»: примените геометрию в конфигураторе, обновите active,
добавьте снимок и повторите для других вариантов. Включите набор и запустите матрицу.
Инструкция и ограничения: [[engineering/RESEARCH_BATCH#Набор носителей в форме]].
