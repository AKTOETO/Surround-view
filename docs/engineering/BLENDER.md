# Метрическая 3D-сцена Blender и четырёхкамерный replay

Проверено 06.10.2026: Blender MCP отвечает, add-on 1.8 / protocol 13 совместимы, Blender **5.2.2 LTS** имеет доступ к проекту. Подключение использовано для создания отдельной сцены `SV Research Street`; исходная пользовательская сцена сохранена. Внешние asset libraries и генераторы на данном подключении отключены. Авторские скрипты создают все объекты самостоятельно.

## Что реализовано

`tools/blender/scene.py` строит метрическую улицу: дорогу, разметку, бордюры, здания, деревья, припаркованные автомобили и столбики. Процедурный автомобиль имеет кузов, остекление, колёса и зеркала; это упрощённая геометрия, точная CAD-модель реального автомобиля ещё нужна. Размер кузова 4.6 × 1.8 м. Blender-сцена использует scripted trajectory `x=2t` м без физики. Отдельный keyboard-driven visual preview находится в `examples/sv-simulator/DriveWorld.qml`; он пока не связан с Blender renderer или server camera producers.

В `tools/blender/rig.py` заданы четыре **разнесённых** оптических центра:

| ID | Камера | Центр в V, м | Азимут |
|---|---|---|---|
| 0 | front | (2.36, 0, 0.85) | 0 |
| 1 | right | (0.35, −1.10, 1.12) | −π/2 |
| 2 | rear | (−2.36, 0, 0.85) | π |
| 3 | left | (0.35, 1.10, 1.12) | π/2 |

Каждая ось наклонена вниз на 12°. Система автомобиля: +X вперёд, +Y влево, +Z вверх; оптическая система: +X вправо, +Y вниз, +Z вперёд. Экспортируется `T_camera_from_vehicle`, а для каждого момента — `T_world_from_vehicle` и `T_world_from_camera`. Камеры располагаются снаружи геометрии кузова и зеркал. Видимые элементы собственного автомобиля могут закрывать часть изображения; произвольные оптические masks в renderer пока отсутствуют.

## Оптический конвейер

Из каждого центра Blender рендерит пять перспективных квадратных граней с FOV 90°. Вспомогательная camera последовательно посещает центры; поза сцены внутри одного набора неизменна. Отрицательная оптическая Z-грань не нужна при `theta_max=1.48 < π/2`. Blender использует ось взгляда −Z и верх +Y; матрица каждого face-camera учитывает это отличие. API камеры: [Blender Camera](https://docs.blender.org/api/5.3/bpy.types.Camera.html); batch rendering: [официальная документация](https://developer.blender.org/docs/handbook/building_blender/python_module/).

`convert.py` преобразует эти изображения в четыре RGB8 PPM 400 × 400. Модель — equidistant частный случай OpenCV fisheye: `fx=fy=128`, `cx=cy=199.5`, `k=0`, `alpha=0`. Обратные лучи вычисляет независимый Python-generator; выбор face выполняется по доминирующей компоненте. Bilinear sampling работает в линейном RGB с последующим sRGB encoding. На краях face применяется clamp; seamless cross-face filtering пока не реализована. Вне `theta_max` входной пиксель чёрный, renderer отдельно проверяет FOV.

```plantuml
@startuml
left to right direction
component "Метрическая улица\nи поза автомобиля" as World
component "4 оптических центра\n5 перспективных граней на центр" as Capture
component "Обратные fisheye-лучи\nlinear RGB sampling" as Convert
artifact "config + PPM + manifest\nпозы и SHA-256" as Data
component "sv-server replay\nUnix control/data" as Server
component "GLES: купол + пол\nвиртуальная камера внутри" as Render
World --> Capture
Capture --> Convert
Convert --> Data
Data --> Server
Server --> Render
@enduml
```

*Рисунок Б.1 — Выполненный offline-путь от объёмной сцены до существующего серверного входа. Сетевая передача producer→server остаётся следующим этапом.*

## Зависимости и запуск

Blender нужен только на машине генерации. Обычный конвертер и dataset smoke требуют Python, NumPy, Pillow; `--depth-truth` дополнительно требует Python-модуль OpenEXR, чтобы декодировать float EXR (OpenCV Python не требуется). Рендерер и native-инструменты используют настоящую C++ OpenCV по [[engineering/BUILD]]. Никаких загрузок через CMake нет; Blender и OpenEXR не входят в runtime-зависимости RPM для Авроры.

Из корня проекта:

```sh
blender --background --python tools/blender/scene.py -- \
  --output artifacts/blender-street-capture --frames 4 --face-size 256 --depth-truth
python3 tools/blender/convert.py \
  --capture artifacts/blender-street-capture --output artifacts/blender-street
python3 tools/blender/validate.py --build build \
  --dataset artifacts/blender-street --output artifacts/blender-validation-ipc
```

Если Blender отсутствует в PATH, указать полный путь к исполняемому файлу. На проверенном ПК: `/home/bogdan/.local/share/Steam/steamapps/common/Blender/blender`. Headless-команда приведена как способ повторения; фактический набор этой серии создан через MCP в работающем Blender с EEVEE. `scene.py` берёт engine текущей сцены. Перед длительной серией зафиксировать его выбор, hardware/backend и версии; GPU-изображения разных движков/драйверов не обязаны совпадать побитово.

В консоли открытого Blender / через MCP:

```python
import sys
sys.path.insert(0, '/path/to/surround-view/tools/blender')
import scene
street = scene.build_scene()
scene.capture(street, '/path/to/new/capture', frames=4, face_size=256, depth_truth=True)
```

MCP вызовы имеют отдельные пространства переменных: в следующем вызове заново импортировать модуль и выбирать сцену через `bpy.data.scenes`. В интерфейсе переключиться на `SV Research Street`, чтобы редактировать созданный мир. Capture проверяет Blender pixel-center conventions на 72 точках до записи кадров. Диапазоны CLI: `frames=1..300`, `face-size=32..2048`, `start-frame>=0`; большие серии требуют соответствующего диска и времени.

Каждый output-каталог должен быть новым. При ошибке неполный каталог остаётся для диагностики; `capture.json` / `manifest.json` публикуется только после успешной генерации соответствующего набора. Конвертер проверяет SHA-256 входных PNG и запрещает выход файлов за capture-каталог.

## Использование в сервере

```sh
EGL_PLATFORM=surfaceless SV_EGL_PLATFORM=surfaceless build/sv-server \
  --config artifacts/blender-street/config.json \
  --manifest artifacts/blender-street/manifest.json \
  --ipc-dir /tmp/sv-blender --trace artifacts/blender-server.jsonl
```

В другом терминале: `build/sv-client /tmp/sv-blender`. См. [[engineering/USAGE]] для управления и EGL backends. Сервер пока читает replay-manifest с фиксированным шагом ≈33.3 ms. Здесь такой шаг соответствует сценарию 30 FPS; это не реальная скорость Blender capture. Расположенные в другом процессе или на другом ПК виртуальные камеры пока не отправляют live-кадры серверу.

`validate.py` проверяет все manifest hashes, изменение каждого входа при движении, запуск `sv-bench`, получение четырёх READY RGBA-кадров сервера, непрозрачность выхода и применение команды ракурса. Сохраняет `smoke.json`, bench-результаты и server trace. Нужен доступ к Unix sockets. Фактический GL vendor/renderer берётся из результата; переменная `LIBGL_ALWAYS_SOFTWARE` сама по себе не гарантирует выбор Mesa при NVIDIA EGL. Для настоящего backend/performance comparison использовать [[engineering/PLATFORM_TEST]].

## Файлы и сохранение мира без LFS

| Файл | Содержимое |
|---|---|
| `tools/blender/scene.py`, `rig.py` | Полный исходник процедурного мира, масштаб, rig и траектория; обычный Git |
| `assets/scenes/metric-street/street.blend` | Сохранённый авторский мир; обычный Git, metadata в `provenance.json` |
| `capture/street.blend` | Снимок сцены очередного capture; результат выполнения в `artifacts/` |
| `capture/capture.json` | Оптика, позы, движок, версии, checksums изображений/EXR/скриптов и optics check |
| `capture/overview.png` | Истинная 3D-сцена для визуальной проверки |
| `replay/config.json`, `manifest.json` | Строго совместимые входы текущего прототипа |
| `capture/depth/*.exr` | Опциональный Blender float-Z для каждого кадра, камеры и используемой cube face; `1e10` означает отсутствие поверхности. `nz` не снимается, так как FOV камер < 180° |
| `replay/depth_camera*_*.npy` | При включённом depth capture — radial range в метрах на fisheye сетке; `NaN` означает отсутствие поверхности |
| `replay/ground_truth.json` | Сценарные позы, происхождение, hashes и карта depth truth при её наличии; semantic labels пока отсутствуют |

По решению пользователя **Git LFS не используется**. Исходный мир сохранён обычным Git в `assets/scenes/metric-street/street.blend` вместе с provenance; его также можно воссоздать скриптами. Каталог и восстановление всех исходных материалов — [[engineering/ASSETS]]. Новые capture-снимки и полные входные серии сохраняются в игнорируемом `artifacts/`. В документацию входят небольшие PNG реальных рендеров. Для переноса на другой компьютер передать архив capture/replay напрямую либо повторить генерацию. Для сравнения качества на разных платформах предпочтительнее один архив с одинаковыми байтами и SHA-256.

Иллюстрации глав:

```sh
python3 docs/diploma/plot_blender.py \
  --capture artifacts/blender-street-capture --dataset artifacts/blender-street \
  --validation artifacts/blender-validation-ipc --low-view artifacts/blender-low-view
```

Последний каталог создаётся отдельным `sv-bench` с той же конфигурацией и `virtual_camera.elevation_rad=0.35`. Остальные параметры сохраняются; исходную запись менять не нужно.

## Следующие этапы

1. Semantic labels и независимые метрические маркеры; отдельные сцены подбора и оценки. Radial-range conversion проверена аналитическими плоскостями, 3D-примитивами (сфера, куб) и силуэтными разрывами глубины; ground/raised markers и discrete semantics реализованы.
2. Измерить ошибки разметки и вертикальных объектов, швы/ghosting и temporal stability; выполнить подтверждающую серию carriers/fusion по [[research/PROJECTION_AND_STITCHING]]. Первый 30-case depth-visibility screening описан в [[validation/STITCH_VISIBILITY]].
3. Связать driving preview из `sv-simulator` с четырьмя live producers по существующим `FrameSource`/socket-input контрактам; сохранять синхронные pose/time/depth truth.
4. Добавить реалистичную модель автомобиля и материалы с фиксированной лицензией; текущий мир процедурный.

## Источники кадров в рабочем профиле 0.6.0

Реализованы replay worker, четыре независимых виртуальных входа Unix/TCP и локальный OpenCV/V4L2 adapter. Нормативное описание полей `source`, producer handshake/type 10, границы времени и очередей: [[engineering/SOURCES]]. Команды Blender → producer → server → client: [[engineering/USAGE#Blender-запись через виртуальные камеры]]. Физическая приёмка камер/драйверов, UDP и подача live-кадров из driving preview остаются открытыми.

## Сквозная проверка virtual-camera producer

```sh
python3 tools/blender/validate.py --dataset artifacts/blender-street \
  --output artifacts/blender-virtual-new --source socket
```

Validator создаёт временные Unix endpoints, запускает настоящий server и отдельный producer, проверяет четыре READY RGBA output и применённый ракурс, сохраняет `server-view.png`, trace/config и `smoke.json`. Output должен быть новым; `--source replay` (default) сохраняет прежнюю проверку. В 0.6.0 оба пути прошли на RTX 5070 Ti: [[validation/SOURCES_SMOKE]]. Это finite recording streaming, не realtime Blender rendering.

Socket validator также сохраняет `producer.json`/`producer.md`; в конце finite smoke он штатно останавливает длительный producer через SIGTERM, поэтому status cancelled/exit 143 ожидаемы. Проверка этого lifecycle — [[validation/PRODUCER_RECOVERY]].

## Правила мира и ошибки крепления камер

07.10.2026 MCP повторно подключён: Blender 5.2.2 LTS, add-on 1.8 / protocol 13. Создана отдельная `SV Research Street` с 327 объектами; исходная пользовательская `Scene` сохранена. Рецепт — `assets/scenarios/mount-errors-v1.json`, готовый мир — `assets/scenes/mount-errors/street.blend`. Предыдущая metric-street не заменена.

```sh
blender --background --python tools/blender/scene.py -- \
  --scenario assets/scenarios/mount-errors-v1.json \
  --output artifacts/mount-capture-new --frames 1 --face-size 128
python3 tools/blender/convert.py --capture artifacts/mount-capture-new \
  --output artifacts/mount-dataset-new
```

Рецепт версии 1 содержит `seed`, `world.building_height_m` (диапазон 3…20 м), `building_spacing_m` (8…15 м), `mounts.yaw_deg`, `pitch_deg` (random half-ranges 0…20°), `along_body_m` (0…0.4 м) и `overrides` с ключами `"0"`…`"3"`. Override задаёт фактическое подписанное отклонение конкретной камеры, остальные значения выбираются uniform из симметричного диапазона. Домены ограничивают этот генератор, а не описывают статистику реальных автомобильных креплений.

Yaw поворачивает оптический базис вокруг +Z автомобиля; pitch — вокруг локальной +X камеры (вправо), положительное значение поднимает взгляд. Сдвиг front/rear идёт вдоль +Y кузова, right/left — вдоль +X, без изменения высоты и расстояния от поверхности кузова. Это одна определённая модель монтажных ошибок, не полный 6-DoF perturbation. Модель автомобиля не перестраивает зеркала под сдвиг камеры; occlusion/self-occlusion ещё надо исследовать.

Без `--scenario` сохраняется прежняя геометрия и номинальный rig. С рецептом vary высота/число этажей зданий, spacing и крепления; дорога/основная планировка остаются заданными генератором. Это параметризованная улица, не произвольный map editor. В `capture.json` добавлены recipe, sampled offsets и nominal config. Конвертер сохраняет отдельный `nominal-config.json`, true config/poses и recipe в `ground_truth.json`; `config.json` описывает фактическую capture calibration для replay. В калибровочный solver нужно передавать nominal config, а true config использовать только для синтеза наблюдений и оценки.

Первое исследование и ограничения: [[research/MOUNT_CALIBRATION]], [[validation/MOUNT_CALIBRATION]]. Laptop GUI пока не содержит редактор этих правил; общий Python module `tools/blender/scenario.py` подготовлен для дальнейшего переиспользования, отдельного launch script не добавлено.

## Изображения калибровочной доски

Опция `--calibration-boards` добавляет в capture 9×6 внутренних углов с размером клетки 0.2 м и меняющимися позами, по одной видимой доске на камеру в каждом кадре. В метаданных для каждого вида сохраняется `T_vehicle_from_board`; начало — нижний левый внутренний угол доски, как требует schema observations. Рендерная сетка имеет белую внешнюю рамку, поэтому для размещения mesh origin сдвигается на одну клетку. Сторона симметричной доски задаётся генератором (`corner_order`), что допустимо для синтетического контроля, но не переносится на физическую доску без асимметричной метки или ручной проверки.

```sh
blender --background --python tools/blender/scene.py -- \
  --scenario assets/scenarios/mount-errors-v1.json \
  --output artifacts/board-capture --frames 4 --face-size 512 --calibration-boards
python3 tools/blender/convert.py --capture artifacts/board-capture \
  --output artifacts/board-data
python3 tools/configurator.py calibrate-images --dataset artifacts/board-data \
  --output artifacts/board-calibration --build build --training-frames 3
```

На проверенном Blender MCP получены 16 кадров, все с 54/54 углами; три позы на камеру используются для fit, четвёртая — только для валидации. Итог: [[validation/IMAGE_CALIBRATION]], постановка исследования: [[research/IMAGE_CALIBRATION]]. Физическая точность, ошибки измерения поз доски, частичное обнаружение и окклюзия пока не проверялись. Если `blender` недоступен как CLI, можно выполнить scene builder/capture через подключённый Blender MCP; генератор не меняет исходную пользовательскую сцену.

## Парный direct-view truth и движение rig

`tools/blender/paired_truth.py::capture_paired` использует активную сцену, делает synchronous capture с `frame_step` на 30-Hz timeline, затем повторяет те же позы для прямого вида виртуальной камеры и ray-cast object IDs. Проекция проверяется независимо через Blender projector. Параметры, ограничения ray casts и команды запуска: [[validation/PAIRED_STITCH_TEMPORAL]]. Входы численного smoke сохранены в `tests/data/paired_street_v1`; новый захват не нужен для повторного расчёта. Первый run — 3 кадра, step 6, то есть 5-Hz sampling. Для качественного исследования увеличить cube-face resolution и длину/разнообразие сцены.

Парный capture теперь экспортирует `visibility_*.npy` (uint8 bit i = видимость scene point из камеры i). Для ray casts исключены `hide_render` helpers: они не должны заслонять объекты, отсутствуя в RGB. `tests/blender/test_visibility.py` проверяет это в Blender временной плоскостью. Доступен lossless `convert.py --image-format png`, а temporal evaluator принимает `--visibility-policy ignore|any|all` (default ignore для прежних fixtures). Команды, matched-resolution experiment и ограничения opaque visibility: [[validation/STITCH_RESOLUTION_VISIBILITY]].

`paired_truth.capture_study(plan_path, output)` создаёт отдельные scene variants из сохранённого JSON plan, восстанавливает active user scene и записывает capture hashes. `near_obstacle_jitter_m` (0..0.8 m) изменяет близкие препятствия, не только здания; actual positions сохраняются в capture/ground_truth. Полная инструкция и результаты: [[validation/STITCH_CONVERGENCE_ROBUSTNESS]].

## Независимые IDs входных камер и диагностическая мишень

`tools/blender/diagnostic.py:build_diagnostic` создаёт изолированную улицу по `assets/scenarios/object-stitch-v1.json`. `capture_paired(..., source_ids=True)` дополнительно сохраняет четыре uint16 object-ID карты на кадр и hashes в paired metadata. `geometry_truth.py` объединяет ray helper для direct/source truth, пропуская hide-render helpers. Exporter ограничен equidistant zero-skew opaque optics; не моделирует RGB filtering/transparency. Двухкадровый Blender regression и восстановление camera state описаны в [[validation/OBJECT_STITCH]]. Там же команды захвата, конвертации и численного воспроизведения.
