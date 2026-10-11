# Каталог исходных ассетов

Редакция 06.10.2026. `assets/` содержит долговременные исходные материалы проекта: скачанные изображения окружения и созданные для симулятора сцены. Эти файлы сохраняются **обычным Git, без Git LFS**. Исходные ассеты должны переживать удаление build/test-каталогов.

## Структура

| Каталог | Назначение |
|---|---|
| `assets/demo/` | Фотографическое окружение для существующего `sv-scene`; этот путь также используется installed/RPM-профилем |
| `assets/scenes/metric-street/` | Авторская объёмная улица Blender и описание её происхождения |
| `tools/blender/` | Исходники генерации мира, rig, конвертера и проверки; это код, а не бинарный ассет |
| `assets/scenarios/` | Текстовые recipes процедурных исследовательских сцен и ракурсов |

Серии входных кадров, capture/replay manifests, traces, bench previews, отчёты, RPM и сборочные архивы относятся к результатам выполнения и остаются в `artifacts/`. Иллюстрации диплома и исследования хранятся рядом с соответствующей документацией; их источники — Python-скрипты глав/исследования. Копировать эти результаты в `assets/` не нужно.

## A01 — Фотографическая улица Лондона

| Поле | Значение |
|---|---|
| Файл | [urban_street_01_1k.hdr](../../assets/demo/urban_street_01_1k.hdr) |
| Источник | [Urban Street 01 — Poly Haven](https://polyhaven.com/a/urban_street_01) |
| Автор | Andreas Mischok |
| Условия использования | [CC0 — Poly Haven](https://polyhaven.com/license) |
| Получен | 05.10.2026 |
| Формат | Radiance HDR, 1024 × 512; equirectangular panorama |
| Размер | 1 840 705 байт |
| Применение | `sv-scene`: четыре fisheye-входа из общего оптического центра |
| Установка | `share/surround-view/assets/demo/urban_street_01_1k.hdr` |

Исходный адрес загрузки:

```text
https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/urban_street_01_1k.hdr
```

SHA-256:

```text
4608de4584ad25b20c87944659841ac5d93162d26bbaa5146e379ad36d6227e0
```

В проекте скачана одна исходная HDR-панорама Лондона. Четыре PPM, формируемые из неё, и GLES-ракурсы в дипломе являются результатами работы программы. Подробности оптической модели и ограничений — [[engineering/SCENE]].

## A02 — Метрическая улица и автомобиль Blender

| Поле | Значение |
|---|---|
| Файл | [street.blend](../../assets/scenes/metric-street/street.blend) |
| Происхождение | Процедурная авторская сцена, созданная для этого проекта через Blender MCP |
| Дата | 06.10.2026 |
| Blender | 5.2.2 LTS |
| Render engine | EEVEE |
| Размер | 2 189 373 байта |
| Внешние модели/текстуры | Нет; геометрия и материалы созданы Python-кодом |
| Проверка файла через Blender | Сцена `SV Research Street`: 300 объектов, 295 meshes, 14 материалов, images отсутствуют |
| Состав | Улица, разметка, бордюры, здания, деревья, столбики, припаркованные машины и ego-автомобиль |
| Camera rig | Четыре разнесённых центра с сохранёнными intrinsics/extrinsics |
| Метаданные | [provenance.json](../../assets/scenes/metric-street/provenance.json) |
| Условия использования | Отдельная лицензия авторских ассетов проекта пока не объявлена; CC0 панорамы A01 на эту сцену не переносится |

SHA-256 сохранённой сцены:

```text
a76745d0ca0b86c791255cbaf3047e2fe8011109b967388de4b3d88e479c79bb
```

Сохранён точный `.blend` последнего проверенного capture: автомобиль находится при x=0.2 м, camera установлена на внешний обзор сцены. Это редактируемый исходный мир; входные RGB-кадры и результаты server smoke в него не включены. Все meshes/materials находятся внутри `.blend`, внешние пути к моделям или HDR для открытия мира не нужны. В render settings может остаться путь вывода предыдущего capture; перед новым рендером выбрать свой output.

`provenance.json` фиксирует исходный коммит генератора `9914d77`, SHA-256 `scene.py`/`rig.py`, размер и hash `.blend`, сохранённую позу и camera config. Если генератор изменится, эта запись продолжит описывать сохранённый снимок; для нового мира создавать новую ревизию ассета и обновлять provenance.

## Открытие и восстановление мира

Открыть сохранённый мир в новом процессе Blender:

```sh
blender assets/scenes/metric-street/street.blend
```

Если `blender` отсутствует в PATH, использовать абсолютный путь к установленному бинарнику. В списке сцен выбрать `SV Research Street`.

Да, мир можно воссоздать скриптами: [scene.py](../../tools/blender/scene.py) содержит геометрию, материалы, освещение и автомобиль, [rig.py](../../tools/blender/rig.py) — масштаб, четыре оптических центра, калибровку и траекторию. В Python-консоли Blender:

```python
import bpy
import sys
sys.path.insert(0, '/path/to/surround-view/tools/blender')
import scene

street = scene.build_scene()
# Новый путь: не перезаписывать исходный asset при эксперименте.
bpy.data.libraries.write('/path/to/new/street-recreated.blend', {street})
```

`build_scene()` создаёт отдельную сцену, поэтому существующий пользовательский мир не удаляется. Повторная генерация возвращает исходную позу автомобиля x=0 и вспомогательной камеры; сохранённый A02 содержит состояние после capture. Геометрия восстанавливается кодом, а точный снимок состояния и ручные правки защищает файл `.blend`. Побитовое совпадение повторно сохранённых Blender-файлов или GPU-рендеров не требуется.

Для экспорта кадров из **сохранённого** мира импортировать модуль и передать загруженную сцену:

```python
import bpy
import sys
sys.path.insert(0, '/path/to/surround-view/tools/blender')
import scene
street = bpy.data.scenes['SV Research Street']
scene.capture(street, '/path/to/new/capture', frames=4, face_size=256)
```

Так сохраняются правки открытого мира. CLI `scene.py --output ...` создаёт новый мир из кода. Конвертация, server replay и ограничения симулятора описаны в [[engineering/BLENDER]].

## Добавление и проверка ассетов

Для каждого нового исходного файла записывать путь, назначение, источник/автора, лицензию, дату, размер, SHA-256 и зависимости. Для авторских миров сохранять генератор или редактируемый исходник и provenance. Ранние дубликаты `street.blend` из smoke-каталогов не являются отдельными ассетами; в Git сохранён один актуальный мир.

Из корня проекта:

```sh
sha256sum assets/demo/urban_street_01_1k.hdr \
  assets/scenes/metric-street/street.blend
```

При пересылке или клонировании оба ассета приходят вместе с обычным Git. `.blend` — host-ресурс для авторинга; он не добавлен в runtime RPM Авроры. Панорама A01 остаётся установленным ресурсом `sv-scene`.

## Seeded mount-error street, 07.10.2026

- `assets/scenarios/mount-errors-v1.json` — авторский рецепт seed 20261007: building heights 4…11 м, spacing 9 м, yaw ±5°, pitch ±4°, slide ±0.15 м; SHA-256 `d0590728799f98cd44f56dc9d33ec04c03fd0b5f673c96351e61e997e67a006e`.
- `assets/scenes/mount-errors/street.blend` — отдельный авторский мир Blender 5.2.2 LTS, 327 объектов, около 2.3 MB, без внешних текстур/моделей; SHA-256 `8acb78ff1f8953f3c6ac03e501b5faf54ee8cd0850b5b27776c19a4748facbc9`. Включает actual/nominal config и sampled offsets в свойствах scene. Сохранён обычным Git, без LFS; прежний metric-street сохранён.

Мир восстановим из `tools/blender/{scene,rig,scenario}.py` и рецепта. Exact `.blend` hash зависит от Blender serialization и не обязан повториться при реконструкции; параметры/геометрия воспроизводятся при той же версии генератора. Capture RGB, synthetic observations, fitted candidates и GLES previews — производные опытов в `artifacts`, не новые исходные assets. Их избранные подписанные иллюстрации и скрипт входят в диплом. Команды — [[engineering/BLENDER]], методика — [[research/MOUNT_CALIBRATION]].

## Компактный парный исследовательский fixture

`tests/data/paired_street_v1` (~1.4 MB): авторские procedural Blender RGB и object-ID карты, сохранённые как входы воспроизводимого опыта. Это generated validation fixture, а не внешний художественный asset; исходный мир остаётся в `assets/scenes/metric-street`. Описание происхождения/файлов — `tests/data/paired_street_v1/README.md`, методика — [[validation/PAIRED_STITCH_TEMPORAL]]. Raw captures и snapshots не включены в Git; компактные рисунки диплома создаёт `docs/diploma/plot_paired_stitch.py`.

`tests/data/stitch_resolution_v1` (~3.45 MB) сохраняет два авторских validation inputs (64/256 cube faces) с direct RGB, object IDs и independent source-visibility bitmaps. Это не внешний asset и не новый мир: оба условия используют ту же процедурную улицу. Raw cubes/snapshots не включены. Описание файлов/хэши — README/JSON внутри fixture; протокол — [[validation/STITCH_RESOLUTION_VISIBILITY]].

`assets/scenarios/stitch-validation-v1.json` фиксирует seeds 12/13/14, near-obstacle jitter и capture parameters. `near_obstacle_positions` восстанавливает координаты столбиков независимым seeded stream. Новые generated миры/snapshots/полные серии остаются в `artifacts/stitch-validation-v1`, не в Git; данные можно повторно получить `capture_study`. Convergence input `tests/data/stitch_resolution_v1/512` занимает ещё ~1.9 MB. Протокол и хэши: [[validation/STITCH_CONVERGENCE_ROBUSTNESS]].

## Blueprint диагностической мишени

`assets/scenarios/object-stitch-v1.json` — авторский procedural план улицы seed 15, автомобиля, номинальных камер и emission cuboid. `tools/blender/diagnostic.py` дополняет общий генератор сцены; внешние изображения/модели не используются. Мир воспроизводится скриптами, `.blend` не требует Git LFS. Компактные производные RGB/ID в `tests/data/object_stitch_v1` являются тестовым fixture; raw capture и пробные позиции остаются в `artifacts/`. Происхождение, ограничения и полный протокол: [[validation/OBJECT_STITCH]].

## Новые procedural cases для seam study

`assets/scenarios/seam-generalization-v1.json` — авторский recipe трёх новых seeded world/mount/target instances (101/102/103). Все meshes/materials создаются checked-in `tools/blender/scene.py`/`diagnostic.py`; внешний .blend, художественные downloads и Git LFS не нужны. Рецепт включает правила окружения, физические target размеры и bounds отклонений камер. Frozen исследовательские условия: [[research/SEAM_GENERALIZATION_PROTOCOL]]. Scientific captures/inputs/reports находятся в игнорируемом `artifacts/seam-generalization-v1`; их provenance содержит recipe/generator hashes. Это generated measurement data, не дополнительные художественные ассеты. Реальный Blender world остаётся отдельной сценой; исходная active scene не удаляется.

## A03 — Рецепт исследования бокового ракурса

`assets/scenarios/carrier-lateral-v1.json` —авторский текстовый recipe трёх street instances с coded targets и view elevation0.35/fov1.6rad. Он сохраняет scenario/mount/target параметры прежнего seam-generalization-v1, явно задавая новый virtual_camera. Внешних downloads нет. Генератор `tools/blender/carrier_study.py` использует прежние scene/rig/paired_truth helpers и создаёт новый complete capture; source/helper hashes сохраняются в provenance.

В Git сохраняются recipe, driver, protocol, compact baseline и иллюстрации диплома. Cubefaces, direct truth, source IDs, fisheye replay, native RGBA/traces/logs —generated artifacts, а не исходные ассеты. Для восстановления нужны Blender и прежние versioned helpers; команды: [[engineering/BLENDER#Боковой ракурс с независимым truth]]. Результаты и ограничения: [[validation/CARRIER_LATERAL]].
