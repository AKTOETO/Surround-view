# Метрическая 3D-сцена Blender и четырёхкамерный replay

29.10.2026 подключение повторно проверено: Blender5.2.3 LTS/EEVEE, add-on1.8/protocol13
(MCP ожидает15, execute_code fallback работает). По frozen E1 matrices получены18 новых
captures/reference sets, image-quality E3 завершена54/54 server conditions.
Raw worlds восстанавливаются procedural scripts и configs/research/thesis-scene-inputs.json;
street.blend сохранён в каждом raw output, Git LFS не требуется. Команды/артефакты:
[[validation/THESIS_E3#Артефакты и воспроизведение выполненного E3]].

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

## Восстановление новых frozen seam scenes

MCP проверен 11.10.2026: Blender5.2.2 LTS, addon1.8, protocol13. Новые recipes находятся в `assets/scenarios/seam-generalization-v1.json`; они воспроизводят meshes/world/camera offsets без .blend и LFS. В Blender console/MCP используйте существующие generators (укажите абсолютный путь своего checkout и свежий output directory):

```python
import bpy, json, sys, hashlib
from pathlib import Path
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path.insert(0, str(root/'tools/blender'))
from diagnostic import build_diagnostic
from paired_truth import capture_paired
plan_path = root/'assets/scenarios/seam-generalization-v1.json'
plan = json.loads(plan_path.read_text())
output = root/'artifacts/seam-repeat'
output.mkdir(exist_ok=False)
sources = ['scene.py','scenario.py','rig.py','diagnostic.py','diagnostic_motion.py','paired_truth.py','visibility.py']
(output/'provenance.json').write_text(json.dumps({
    'plan_sha256': hashlib.sha256(plan_path.read_bytes()).hexdigest(),
    'generator_sha256': {n:hashlib.sha256((root/'tools/blender'/n).read_bytes()).hexdigest() for n in sources},
    'blender_version': bpy.app.version_string}, indent=2)+'\n')
original = bpy.context.window.scene
try:
    for case in plan['cases']:
        variant = build_diagnostic(case)
        bpy.context.window.scene = variant
        seed = case['scenario']['seed']
        capture_paired(variant, output/f'seed{seed}-capture', **case['capture'])
finally:
    bpy.context.window.scene = original
```

Каждый capture каталог должен быть новым. Внешний `artifacts/seam-repeat` создаётся в приведённом коде; не подмешивайте файлы прежней серии. Для исследовательского provenance перед capture также сохраните SHA-256 recipe/generator файлов и версию Blender; автоматический read-only study audit требует provenance.json из исходного опыта. World builder не удаляет пользовательскую сцену. Конвертация после завершения capture:

```sh
python3 tools/blender/convert.py --capture artifacts/seam-generalization-v1/seed101-capture --output artifacts/seam-generalization-v1/seed101-inputs --image-format png
```

Повторите для seeds102/103. Используйте **actual perturbed config** из inputs, а не nominal-config, если цель — сравнение сшивки при известной калибровке. Это не испытание восстановления параметров calibration solver. Research matrix/ограничения: [[research/SEAM_GENERALIZATION_PROTOCOL]].

## Боковой ракурс с независимым truth

Inputs recipe: `assets/scenarios/carrier-lateral-v1.json`; protocol [[research/CARRIER_LATERAL_PROTOCOL]]. В Blender Python Console или через MCP выполнить, указав абсолютный путь проекта:

```python
from pathlib import Path
import sys
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path.insert(0, str(root/'tools/blender'))
from carrier_study import capture_study
capture_study(root/'assets/scenarios/carrier-lateral-v1.json', root/'artifacts/carrier-lateral-repeat-inputs')
```

Драйвер создаёт новый каталог, сохраняет plan/helper hashes и progress.json, строит три isolated coded-target scenes, задаёт virtual_camera до paired capture и восстанавливает исходную активную Scene в finally. Камерные cubefaces и direct RGB/object-ID/visibility/source IDs снимаются заново. Не переиспользуйте уже существующий output и не меняйте frozen scripts/plan между capture и audit. При сбое progress содержит failed/error, неполную запись нельзя включать в matrix. Generated data остаются в artifacts; source recipe/driver сохраняются обычным Git, без LFS.

После завершения `carrier_lateral.py --run-binaries` конвертирует captures в fisheye PNG, если inputs ещё нет, и выполняет native matrix: [[engineering/USAGE#Сшивка в боковом ракурсе]]. Для прочих ракурсов создавайте новую версию recipe/protocol; прежний direct truth нельзя использовать после изменения virtual_camera.

## Управляемая высота и расстояние объекта

После подключения Blender выполнить в Python Console, указав путь проекта:

```python
from pathlib import Path
import sys
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path.insert(0, str(root/'tools/blender'))
from parallax_study import capture_study
capture_study(root/'assets/scenarios/parallax-height-v1.json', root/'artifacts/parallax-height-repeat-inputs')
```

Шесть isolated scenes сохраняют автомобиль, camera locators, дорогу100×100m и box0.4m. Остальные meshes удаляются только из новых сцен. Варьируются высота центра и расстояние box; исходная active Scene восстанавливается. Это идеализированный стенд, не новая городская карта. Root progress должен иметь state=complete. Provenance фиксирует master/helper hashes, local case plans и retained mesh inventory. Запускайте capture и timing trials последовательно. Команды native matrix: [[engineering/USAGE#Управляемая высота и параллакс]]. Ограничения: [[research/PARALLAX_HEIGHT_PROTOCOL]].

## Полный объект в кадре: два варианта монтажа

Recipe `assets/scenarios/parallax-framed-v1.json` использует seeds101/104,12 cases, view around origin (azimuth0.8/elevation0.6, distance10m/fov_y1.6rad). Blender Console:

```python
from pathlib import Path
import sys
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path.insert(0, str(root/'tools/blender'))
from parallax_study import capture_study
capture_study(root/'assets/scenarios/parallax-framed-v1.json', root/'artifacts/parallax-framed-repeat-inputs')
```

Тот же versioned generator сохраняет helper/master/local-plan hashes, retained mesh inventory и progress. Активная пользовательская Scene восстанавливается. Требуется новый output каталог. После state=complete используйте [[engineering/USAGE#Полностью помещающийся в кадр объект и два монтажа]]. Legacy Blender virtual_pose умеет optional target_m, но production config этого поля не поддерживает: не добавляйте его в server study. Frozen framed validator отвергает такое расширение; перед финальной серией пять native configurations проверены geometry probe. Протокол: [[research/PARALLAX_FRAMED_PROTOCOL]].

## Парный контроль разрешения cubemap

Новый recipe `assets/scenarios/parallax-sampling-v1.json` сохраняет все 12 framed cases, меняя только грани cubemap 256→512. Выполнить в Blender Console после загрузки прежних imports:

```python
capture_study(root/'assets/scenarios/parallax-sampling-v1.json', root/'artifacts/parallax-sampling-inputs-v1')
```

Выходной каталог должен отсутствовать. Не менять старые helpers или framed asset: парный audit сверяет их hashes, полные конфигурации/позы и decoded direct RGB/object-ID/visibility. Старые 256px captures сохраняются для сравнения; переснятая серия не подменяет их. Дождаться `progress.json: state=complete` и остановить capture перед native trials. Протокол: [[research/PARALLAX_SAMPLING_PROTOCOL]], команды: [[engineering/USAGE#Парный контроль разрешения cubemap]].

## Независимый truth640×360

Recipe `assets/scenarios/parallax-output-v1.json` сохраняет512px cubemap и400px fisheye, меняя только output320×180→640×360. Прежние imports/`root` из раздела paired capture:

```python
capture_study(root/'assets/scenarios/parallax-output-v1.json', root/'artifacts/parallax-output-inputs-v1')
```

Используется прежний неизменённый driver/helpers. Новый direct RGB/ID/visibility вычисляется заново для новых pixel centers; нельзя resize старый truth. Дождаться `state=complete` в progress и остановить capture перед native trials. Сравнение принимает только побитно прежние source camera RGB. Blender PNG содержит переменные Date/Camera/Scene/RenderTime: file hashes могут различаться при одинаковом содержимом, но manifest checks не отключаются. Подсчёт cube faces идёт по фактическим `frames[].cameras[].faces`, исключая overview; этот exporter сохраняет5 нужных граней на камеру. [[engineering/USAGE#Чувствительность к выходному разрешению]], [[research/PARALLAX_OUTPUT_PROTOCOL]].

## Fisheye800 при прежнем поле зрения

Recipe `assets/scenarios/parallax-sensor-v1.json` сохраняет мир, монтаж, cubemap512 и выход640×360 предыдущей серии. В Blender Python Console:

```python
from pathlib import Path
import sys
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path.insert(0, str(root/'tools/blender'))
from parallax_sensor import capture_sensor_study
capture_sensor_study(root/'assets/scenarios/parallax-sensor-v1.json', root/'artifacts/parallax-sensor-inputs-v1')
```

Нужен новый выходной каталог. Дождитесь `progress.json: state=complete`, затем остановите Blender capture перед серверными измерениями. `camera_sampling.py` удваивает focal lengths в pixels и переводит principal point по правилу центров пикселей; actual/nominal extrinsics и angular bound сохраняются. Прямые RGB/ID/visibility снимаются заново. Это синтетическая передискретизация прежних cubemap512, а не более детальный физический сенсор. Проверки равенства эталонов и source cubes: [[research/PARALLAX_SENSOR_PROTOCOL]]. Native команды: [[engineering/USAGE#Чувствительность к разрешению fisheye]].

## Независимые семейства окружения и раздельные seeds

`assets/scenarios/quality-families-v1.json` задаёт validation pilot: street-a/b и cargo yard-a/b.
`world.family` выбирает street/cargo_yard. `scene_seed` управляет окружением и near-obstacle
stream, `mount_seed` — yaw/pitch/along-body randomization. Если они отсутствуют, используется
прежний seed; старые recipes не переписываются. Noise seed/модель сенсорного шума ещё не добавлены.
Параметры building_height/building_spacing действуют на street; cargo geometry описана
`tools/blender/environments.py`. В обоих семействах сохранены автомобиль и четыре camera locators.

В Blender Python console либо через MCP:

```python
from pathlib import Path
import sys
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path.insert(0, str(root / 'tools/blender'))
import paired_truth
paired_truth.schedule_family_plan(
    root / 'assets/scenarios/quality-families-v1.json',
    root / 'artifacts/quality-families-new')
```

Output обязан быть новым. Экспорт выполняется case за case через Blender timers; состояние
публикуется в job.json (queued/running/complete/failed/cancelled). До создания output проверяются
schema,1…16 безопасных уникальных IDs, recipes и bounded capture settings. Для отмены создайте
файл cancel в корне job: отмена действует между cases, не прерывает bpy render посередине.
Существующие scenes и открытый .blend не удаляются/не перезаписываются. После job активна
последняя созданная scene; исходная Scene остаётся доступной. Не закрывайте Blender до complete.

В каждом case/raw: perspective faces, overview, legacy-named street.blend (снимок именно данного
случая), capture.json, direct virtual RGB, object IDs и visibility. Metadata содержит recipe,
actual camera config, nominal config, sampled mount offsets, environment family/layout и hashes
генераторов. Для cargo origin=blender_metric_cargo_yard_v1. Нужен Blender с доступным EEVEE;
испытан5.2.2 LTS. Получение тех же seeds не гарантирует битовое совпадение RGB на иной версии
Blender/NumPy/GPU; сравнивайте записанные параметры/versions/hashes и сохраняйте исходные данные.

После complete конвертируйте каждый case, выбрав новые dataset directories:

```sh
python3 tools/blender/convert.py \
  --capture artifacts/quality-families-new/yard-a/raw \
  --output artifacts/quality-families-new/yard-a/dataset --image-format png
```

Обычный просмотр native результата:

```sh
build/sv-server --config artifacts/quality-families-new/yard-a/dataset/config.json \
  --manifest artifacts/quality-families-new/yard-a/dataset/manifest.json --ipc-dir /tmp/sv-yard-a
# В другом терминале:
build/examples/sv-simulator/sv-simulator --unix /tmp/sv-yard-a
```

Сервер/симулятор для просмотра остановите до изолированного исследования. Сквозной fixture/oracle
harness подготавливает внешний schema2 reference, а алгоритмы/metrics исполняет общий C++ runner:

```sh
python3 tests/test_research_quality_fixture.py build \
  --dataset artifacts/quality-families-new/yard-a/dataset \
  --capture artifacts/quality-families-new/yard-a/raw --visibility-policy any \
  --scenario configs/research/family-carrier-screen.json \
  --output artifacts/quality-families-new/yard-a/carrier-screen
```

Он запускает отдельный local sv-server с коротким временным IPC path независимо от глубины
каталога отчёта. Сценарий фиксирует20 variants,2 frames,1 warmup/3 measured blocks; output и
capture paths новые. Python здесь exporter/independent oracle, продуктовая обработка не перенесена
в него. Полноценный compiled multi-dataset supervisor и GUI сценариев ещё предстоят.

Для четырёх завершённых cases:

```sh
MPLCONFIGDIR=/tmp/sv-mpl-native-quality python3 docs/diploma/plot_native_quality.py \
  --suite artifacts/quality-families-new \
  --output docs/diploma/figures/experiments/family_carrier_deltas.png
```

Plotter повторно проверяет plan/capture identities, одинаковые модели/проекции/scenario,
active mesh budget и RGB repeats, собирает carrier-summary.json из native reports и рисует
парные изменения. Для отдельного carrier используйте --run CASE/carrier-screen --carrier dome_floor_v1.
Результаты пилота и ограничения: [[validation/FAMILY_CARRIER_SCREEN]].

При создании мира камера обзора теперь сразу расположена над автомобилем: `(10, -12, 10)` м,
с направлением на `(1, 0, 0.4)` и lens 32 мм. Это исходный вид для просмотра, который
восстанавливается после paired capture; калиброванные камеры используют свои явные позы и оптику.
Исправление не меняет сохранённые изображения пилота. В живом Blender проверены 72 проекции:
максимальная ошибка 0,00004753 px при допуске 0,001 px; возврат матрицы камеры отличается
не более чем на 5,96·10⁻⁸ из-за float round-trip. Калибровка и lens сохранены.

Все основные PNG renders проходят `render_io.render_still`: оператор должен вернуть `FINISHED`,
а ожидаемый файл — существовать и быть непустым. `CANCELLED`, отсутствующий или пустой файл
прерывают экспорт до публикации completion metadata. Это проверка завершения/наличия, не
доказательство корректности содержимого: декодирование и hashes проверяются следующим этапом.
Fault injection покрывает эти отказы без Blender; реальный preview render также прошёл проверку.

## Точные позы E1 для E3

`plot_thesis_calibration.py prepare-e3` подготавливает два family plans из frozen
scene mapping и **завершённых** native E1 candidates. Case может содержать optional
camera_rig={nominal,actual,provenance}. Он задаёт actual capture pose напрямую, без
legacy sampler; непустые mount bounds/overrides вместе с rig запрещены. Существующие
case без поля сохраняют прежний путь. Family capture допускает1…8 frames, в том числе
один static E3 frame; верхний предел16 cases на plan не менялся.

Preflight/native config checks без Blender: [[validation/THESIS_E3]]. Когда Blender/MCP
доступен, выполнить в его console (каждый capture output новый):

```python
from pathlib import Path
import importlib, sys
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path.insert(0, str(root / 'tools/blender'))
for name in ('camera_rig', 'scenario', 'scene', 'paired_truth'):
    importlib.reload(importlib.import_module(name))
import paired_truth
paired_truth.schedule_family_plan(
    root / 'artifacts/e3-new-prepared/capture-high.json',
    root / 'artifacts/e3-high-new')
```

Дождаться job.json complete, затем аналогично выполнить capture-low.json в другом
output. Не менять helpers/inputs во время render. Старые scenes не удаляются; обычные
правила cancellation/progress остаются. capture.json сохраняет camera_rig_provenance
и camera_rig.py SHA; последующая проверка должна сравнить фактические matrices с E1,
а не доверять только labels/seed.29.10.2026 adapter проверен на18 новых runtime captures:
config/provenance/recipes/intended optical matrices и независимые projector checks совпали;
подробные результаты и ограничения float precision — [[validation/THESIS_E3]].
Estimated camera profiles не используются для
генерации source RGB или direct reference: они нужны только исследуемому серверу.
