# Работа с прототипом: от первого кадра до камер и калибровки

Сначала [[engineering/BUILD]]. Для объёмной улицы с разнесёнными камерами и движением — [[engineering/BLENDER]]. Для переносимого отчёта — [[engineering/PLATFORM_TEST]], для RPM — [[engineering/AURORA]]. Команды ниже выполняются из корня проекта; `build/` можно заменить installed `bin/`, а config — установленным файлом. CLI-программы сообщают usage при отсутствии обязательных аргументов; неизвестные параметры считаются ошибкой.

## Универсальный клиент и TCP

В 0.5.0 GUI использует `sv-client-lib`. Настройка `connections`, Unix-only/TCP-only/combined, команды запуска с Blender и внешний CMake consumer: [[engineering/CLIENT_LIBRARY]]. Без блока connections прежние Unix-команды ниже сохраняются. `state` запрашивает состояние; replay следует интервалам manifest, на wrap используется первый интервал (для одного ряда — 33.333333 ms).

Laptop GUI принимает `--unix DIR` или `--tcp HOST CONTROL_PORT DATA_PORT`; без аргументов он ищет локальные Unix IPC endpoints в ограниченном списке стандартных мест. Панель соединения позволяет редактировать endpoint, timeout/reconnect/retries, а Qt сохраняет последние значения. GUI показывает доступную frame/source telemetry, но сервер пока не предоставляет общего API чтения и изменения конфигурации. Подробности discovery и его ограничения: [README sv-simulator](../../examples/sv-simulator/README.md).

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

Вывод — JSON ACK; exit 0 accepted, 2 arguments, 3 connection/timeout, 4 rejected. По умолчанию общий deadline 5000 ms; `--timeout-ms` задаётся перед командой. GUI следует закрыть перед запуском CLI: сервер пока односессионный. CLI открывает legacy data channel и освобождает кадры; calibration job commands доступны через generic command path. Typed runtime config/subscription APIs и полный покадровый trace остаются в плане. Библиотека не повторяет CLI мутацию после потери соединения. `sv-simulator` поддерживает Qt 6 GUI и визуальную driving scene, но live camera production и редактор наблюдений ещё не реализованы.

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
