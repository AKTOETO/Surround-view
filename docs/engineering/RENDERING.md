# Поверхности, слияние и диагностика рендера

Актуализировано 10.10.2026 для Linux-профиля 0.6.x. Связи: [[engineering/USAGE]], [[research/PROJECTION_AND_STITCHING]], [[validation/SURFACE_SCREENING]], [[prototype/STATUS]]. Калибровка и исходные четыре кадра остаются общими для вариантов. Сервер использует прямую проекцию на поверхность; предварительная panorama/cubemap-текстура не создаётся.

## Конфигурация слияния

Необязательное поле корневого JSON:

```json
"fusion": {
  "mode": "angular_feather",
  "diagnostic": "color",
  "edge_width_px": 24.0,
  "angle_power": 2.0
}
```

При отсутствии блока сохраняется прежнее `edge_feather`, `color`, ширина 24 px и степень 2. Внутри блока обязательна только `mode`. Неизвестные ключи/значения отвергаются. Ширина должна быть конечной и находиться в (0,4096], степень — в (0,32]. Настройки меняются при запуске и через `configure_fusion`; поверхность — через `configure_surface`. Контракт: [[engineering/PROTOCOL_IMPLEMENTED]]. Дополнительные параметры: `pyramid_levels` 1..8 (default 4), `smoothness_weight` 0..100 (default 0.1).

Для доступной камеры проверяются глубина, угол fisheye и границы изображения. Пусть `e` — минимальное расстояние проекции до четырёх краёв кадра, `theta` — угол относительно оптической оси. Краевой вес `b = clamp(e / edge_width_px, 0, 1)`, угловой вес `a = cos(theta)^angle_power`.

| Режим | Выбор/вес | Ограничение |
|---|---|---|
| `edge_feather` | Вес b, нормированная сумма | Усреднение не исправляет параллакс |
| `angular_feather` | Вес b × a | Предпочтение оси камеры — эвристика, не измеренная карта качества |
| `seam_distance_feather` | Нормированные расстояния EDT до границы validity | Screen-space расстояние зависит от view |
| `graph_cut_seam` | Default binary_pairs: s-t cut в exactly-two overlap; optional alpha_expansion: joint weighted Potts | Legacy 3+ centrality ties сохранены; новый bounded solver исследовательский |
| `multi_band` | Laplacian colors / Gaussian edge masks | Не восстанавливает глубину и не устраняет ghost автоматически |
| `graph_cut_multi_band` | Cut mask → Gaussian sigma 2 → pyramid blend | Те же ограничения cut, дополнительная CPU стоимость |
| `hard_best_angle` | Единственная валидная камера с максимальным a | Резкий шов; при равенстве выбирается меньший ID |

Два feather-режима декодируют выбранные цвета из sRGB, смешивают в linear RGB и кодируют обратно. Если сумма весов ≤1e-6, используется непрозрачная заливка. Аппаратная фильтрация RGB8-текстур выполняется до декодирования: это не полностью линейная интерполяция источника. Hard-режим выводит выбранную выборку без межкамерного усреднения. Distance feather, binary graph-cut и multiband доступны в сервере через гибридный GLES/CPU путь; компенсация экспозиции ещё не реализована. Новые методы работают в координатах конечного viewport: это не заранее сшитая panorama. RGB слоёв квантован в sRGB8, edge weights — шагом 1/254; CPU blend выполняется в linear RGB. Невалидные направления сохраняют прежнюю заливку, ego vehicle накладывается с учётом carrier depth. Математика и пределы: [[validation/NATIVE_FUSION]].

## Диагностические изображения

| `diagnostic` | Значение пикселя |
|---|---|
| `color` | Обычный результат, автомобиль и opaque fallback |
| `coverage` | Число валидных доступных камер / 4: серый 0,64,128,191,255 |
| `weights` | Нормированная сумма palette-цветов камер; hard показывает выбранный ID |

Palette: ID 0 красный, 1 зелёный, 2 синий, 3 жёлтый. Это визуализация весов, не однозначный способ восстановить все четыре веса из RGB. В диагностике автомобиль не рисуется, его маска на полу выделяется magenta. Чёрный coverage означает поверхность без наблюдения; фон вне геометрии имеет другой цвет. Coverage считает валидность до вычисления веса: валидная выборка на границе может иметь нулевой вес. Поэтому coverage и итоговое участие камеры различаются. Ни диагностическое покрытие, ни фон не являются depth/visibility truth.

## Геометрия оболочек

Все оболочки ориентированы наружу; камера находится внутри, culling не включён. Пол рисуется отдельно, depth buffer совместный. Маска кузова применяется только к полу.

| Тип | Обязательные поля блока `surface` |
|---|---|
| `dome_floor_v1` | `dome_radius_m`, `dome_latitude_cells`, `dome_longitude_cells`, `floor_radial_cells` |
| `cylinder_floor_v1` | `radius_m`, `height_m`, `vertical_cells`, `angular_cells`, `floor_radial_cells` |
| `cube_floor_v1` | `half_extent_m`, `height_m`, `face_cells` |
| `rectangular_bowl_v1` | Прежние поля прямоугольной плоской зоны, внешних размеров, высоты углов и `uniform_cells` |

Каждый блок также содержит `type`. Цилиндр состоит из круглого пола, боковой стены и верхнего диска. Кубический носитель — квадратный пол, четыре стены и крыша; это геометрическая оболочка, **не cubemap pipeline**. Купол — верхняя полусфера и круглый пол. Bowl при высоте углов 0 становится плоскостью.

Пример замены `surface` в config, полученном Blender-конвертером:

```json
"surface": {
  "type": "cube_floor_v1",
  "half_extent_m": 12.0,
  "height_m": 12.0,
  "face_cells": 32
}
```

Для цилиндра vertical_cells 2…256, angular_cells 16…512, floor_radial_cells 4…256; для куба face_cells 2…128. Размер основания обязан вмещать footprint ТС, высота больше 0.5 м. `safe_view` используется и parser, и командами сервера: минимальный зазор 0.25 м до пола/стен/крыши, для купола — до сферы. Прежние ограничения elevation 0.35…π/2 и distance 6…18 м сохраняются. Недопустимый ракурс отвергается; оболочка не гарантирует наблюдаемость всех её точек четырьмя камерами.

## Воспроизводимое сравнение

После экспорта и конвертации мира по [[engineering/BLENDER]]:

```sh
python3 tools/compare_surfaces.py \
  --config artifacts/blender-street/config.json \
  --manifest artifacts/blender-street/manifest.json \
  --output artifacts/surface-screen --iterations 3 --warmup 1
python3 docs/diploma/plot_screening.py --screening artifacts/surface-screen
```

Выходной каталог должен быть новым. Нужны NumPy/Pillow, для рисунков также Matplotlib. Стенд проверяет хэши записи, запускает `sv-bench` для пяти носителей, двух ракурсов и трёх fusion-режимов: 70 сочетаний с color/weights/coverage. Coverage вычисляется один раз на поверхность/ракурс, поскольку не зависит от fusion. Сохраняются config каждого варианта, actual PNG/PPM, metrics, SHA-256 бинарника/скрипта/входов, `report.json` и Markdown `REPORT.md`. Используется только первый набор кадров; плотности сеток различаются. Это screening работоспособности и покрытия, не ranking качества или sustained FPS.

Для интерактивного просмотра одного варианта:

```sh
SV_EGL_PLATFORM=surfaceless build/sv-server \
  --config artifacts/surface-screen/cube-low-angular_feather-color.json \
  --manifest artifacts/blender-street/manifest.json --ipc-dir /tmp/sv-cube
# В другом терминале:
build/sv-client /tmp/sv-cube
```

Для карты покрытия заменить имя config на `cube-low-edge_feather-coverage.json`. Пауза/поворот работают как раньше.

## Проверки

`render_modes` проверяет независимые ожидаемые цвета: linear blend равных вкладов, угловое смещение весов, deterministic tie, отказ каждой камеры и coverage 0…4. Отдельно 36 ракурсов проверяют три замкнутые оболочки с отсутствующими входами: каждый пиксель — непрозрачная поверхность или маска, без просветов фона. Проверки используют канонический fixture, а не параметры пользовательской сцены. Эти же функции включены в native suite для целевого устройства, без Python. Численные пороги и остальные критерии: [[engineering/PLATFORM_TEST]].

## Независимый аналитический CPU-эталон

Добавлен host-инструмент `tools/reference.py` (06.10.2026), NumPy/Pillow, без OpenGL, C++ math и треугольной сетки. Он предназначен для исследования дискретизации носителя. `tools/compare_reference.py` использует те же параметры вариантов, что screening, но вычисляет геометрию независимо. Утилиты запускаются из checkout на Linux; в RPM и SDK-зависимости они не добавляются.

```sh
python3 tools/reference.py \
  --config artifacts/blender-street/config.json \
  --manifest artifacts/blender-street/manifest.json \
  --frame 0 --output artifacts/reference-single
python3 tools/compare_reference.py \
  --config artifacts/blender-street/config.json \
  --manifest artifacts/blender-street/manifest.json \
  --output artifacts/reference-comparison
MPLCONFIGDIR=/tmp/sv-reference-mpl python3 docs/diploma/plot_reference.py \
  --reference artifacts/reference-comparison --view low
```

Выходные каталоги должны быть новыми. Первый инструмент принимает рабочую конфигурацию schema_version 1, manifest с четырьмя canonical camera IDs, совпадающими calibration IDs и SHA-256 всех выбранных изображений; выбранный набор должен иметь нулевые offsets. Разрешение каждого изображения проверяется. Это исследовательский инструмент для предварительно проверенной конфигурации проекта; полного строгого C++ parser он не заменяет. `--frame` выбирает индекс строки manifest, по умолчанию 0.

Каждый пиксель задаёт луч через центр пикселя; строки идут сверху вниз. Выбирается ближайший положительный корень в пределах near/far по глубине вдоль оси виртуальной камеры. Полусфера решается квадратным уравнением, пол — пересечением плоскости с диском; для цилиндра добавлены боковая поверхность и крышка, для куба — шесть ограниченных плоскостей. Bowl решается на девяти аналитических квадратичных участках с проверкой границ; высота 0 даёт ограниченную прямоугольную плоскость. Параметры `*_cells` не влияют на результат CPU-эталона.

Проекция использует `atan2(rho,z)`, четыре коэффициента fisheye, FOV/z/raster validity; билинейная выборка RGB8 предшествует sRGB decode, как в текущем shader. Реализованы три fusion-режима. Равенство hard-best-angle разрешается в пользу меньшего ID. Coverage считается до feather weights; валидная выборка может иметь нулевой вес. Результат всегда сохраняет цветовое изображение, независимо от `fusion.diagnostic`.

| Файл каждого случая | Содержание |
|---|---|
| `reference.png` | Цвет аналитической поверхности; без автомобильного overlay |
| `coverage.png` | Preview: число допустимых камер ×63; точные значения 0…4 находятся в NPZ |
| `evaluation-mask.png` | Carrier hit за вычетом footprint ТС с margin |
| `reference.npz` | `rgb`, `world`, `hit`, `distance`, `coverage`, четыре нормированных `weights`, `evaluation` |
| `report.json`, `REPORT.md` | Хэши config/manifest/implementation/inputs, индекс/время кадра, coverage histogram, ROI и ограничения |

`distance` — параметр нормированного луча в метрах, `inf` при miss. `world` при miss содержит origin как техническое значение; использовать его можно только вместе с `hit`. `weights` равны нулю при отсутствии положительного вклада. Пиковая память растёт с числом выходных пикселей: текущий инструмент векторизует полный кадр и не предназначен для realtime или малопамятного устройства.

Сравнительный прогон создаёт 30 случаев: пять носителей × два ракурса × три режима. Камера: azimuth 0.8 rad, distance 8.5 m, elevation 1.0 / 0.35 rad; прочие view/output/input параметры наследуются из config. Прогон сохраняет эффективный JSON каждого случая и общий отчёт. Время `render_seconds` — один host CPU вызов с подготовленными изображениями, без file decode/NPZ; оно не является устойчивым benchmark или сравнением с GPU.

Ограничения: глубина **носителя** не является глубиной реальных объектов Blender; сам `reference.py` не вычисляет заслонение и физическую видимость. Отдельный host-инструмент ниже делает depth-consistency screening. Footprint не исключает весь силуэт кузова в GPU render. Для image-quality compare GPU/CPU всё ещё нужны одинаковые кадр/config/view, object-ID/semantic truth и корректная обработка background/silhouette. Отсутствие carrier misses не означает наблюдаемость всех поверхностей четырьмя камерами. Первичные результаты геометрии и depth visibility: [[validation/ANALYTIC_REFERENCE]], [[validation/STITCH_VISIBILITY]].

## Depth-truth visibility screening

`tools/compare_visibility.py` — host-only исследовательский consumer четырёх RGB и четырёх radial-depth карт одной синхронной Blender frame. Он проверяет checksums/calibration/timestamps, повторяет analytic CPU render для 5 carriers × 2 views × 3 fusion modes, затем сравнивает ожидаемую дальность до каждой carrier point с измеренной глубиной на sampled source pixel. Tolerance defaults: `max(0.05 m, 1% range)`. Это не добавляет depth-aware decision в production renderer; инструмент только оценивает геометрическую согласованность текущего веса blend.

```sh
python3 tools/compare_visibility.py \
  --dataset artifacts/blender-depth-truth-dataset-fixed \
  --output artifacts/stitch-visibility-v3
```

Требуются NumPy и Pillow, данные должны содержать `config.json`, `manifest.json`, `ground_truth.json`, четыре RGB-файла и четыре проверяемых NPY depth maps. Report содержит source hashes, coverage, доли matching/occluded/no-return weights и ограничения; одна heatmap сохраняется как `visibility-dome-low-edge_feather.png`. Методика и результаты: [[validation/STITCH_VISIBILITY]]. Сейчас прогон использует один Blender frame с face-size 32 и analytic CPU reference; он не даёт seam/ghosting/temporal ranking и не проверяет GLES server readback.


## Численная инспекция гибридного рендера

`Renderer::render(set, view, RenderInspection*)` позволяет явно сохранить actual projected linear RGB/validity/edge weights, базовый fallback RGBA и ego overlay. Тип объявлен в `sv/render_inspection.hpp`. По умолчанию pointer=nullptr: capture не выполняется. Матрицы принадлежат OpenCV через reference counting; API вызывается на render thread и не является сетевой subscription. Для трёх local GPU modes samples отсутствуют. Полный raster oracle и реальный SV01 parity используют этот путь: [[validation/NATIVE_FUSION]].

В multiband RGB вне validity обнуляется перед Gaussian filtering; проверена независимость output от hidden invalid RGB. Это явная конвенция продолжения изображения, а не восстановление наблюдений. На границах она может давать тёмные полосы; исследование других способов продолжения/нормированного сглаживания остаётся отдельной задачей. `weights` для graph_cut_multi_band показывает исходную cut mask до smoothing; она не является полной картой вкладов всех частотных уровней.

### Многометочный seam solver

`fusion.seam_solver=binary_pairs|alpha_expansion` применяется в graph_cut_seam и graph_cut_multi_band. Catalog v4 объявляет integer_potts_v1 и восемь sweeps. Default field binary_pairs может отсутствовать в snapshot для совместимости; optional alpha передаётся явно. Global observed graph включает anchors и 3/4-camera overlaps; masks unavailable labels запрещены. В multi-band output Gaussian blur/pyramid следуют после hard labels. Actual frame содержит seam_optimization с convergence/energy, но energy не является RGB quality score. Формулы, budgets, tie policy и проверенные отрицательные результаты: [[validation/MULTILABEL_SEAM]].

## Учёт бюджета носителя

`Renderer::mesh_resources()` и frame `mesh_resources` содержат `scope=carrier_position_index_buffers`, объекты `active`/`resident` с vertices, indices, triangles, vertex_buffer_bytes, index_buffer_bytes, buffer_bytes. Active учитывает текущую surface; resident также учитывает неактивные carrier meshes, остающиеся после переключений. Это payload размеров `glBufferData` для float3 positions/unsigned indices, а не full GPU memory или RSS; textures/FBO/depth/ego/CPU capacity/driver overhead исключены. Для чистых resource comparisons запускайте новый server process на carrier. Known-answer counts/retained buffers и live Unix/TCP metadata проверены; первая budget series: [[validation/CARRIER_BUDGET]].

### Opt-in carrier positions

Четвёртый аргумент `Renderer::render` — `RenderInspectionRequest`; при `carrier_positions=true` capture также сохраняет CV_32FC3 координаты perspective-interpolated actual mesh в vehicle metres/top-left raster. Background/ego получают NaN, footprint сохраняет geometric position. Запрос требует непустого inspection destination. Координаты получены тремя дополнительными depth-tested RGBA8 passes через float-bit packing; float-color framebuffer extension для этого пути не нужен. Integer precision highp, dithering выключен. Без явного запроса positions пусты.

Inspection сохраняет прежнюю geometry policy с ego model также для weights/coverage diagnostic views; не следует автоматически приравнивать этот слой к final diagnostic RGB/depth. Обычный RGBA завершается до diagnostic passes, regular stage timers не являются полной оценкой их overhead. Матрицы владеют памятью, вызовы остаются render-thread-only. Первый native accuracy/parity pilot: [[validation/CARRIER_POSITIONS]], команды/export layout: [[engineering/USAGE#Фактические координаты носителя]]. Source projection добавлена следующим расширением ниже; contributing support, scene correspondence и typed wire workflow остаются открытыми.

### Opt-in source projections

`RenderInspectionRequest::source_projections=true` сохраняет четыре `SourceProjectionInspection`: `pixels` (CV_32FC2, координаты центров пикселей исходной камеры) и `status` (CV_8UC1). Начало всех output matrices — top-left. Для texture lookup координаты преобразуются как `(pixels + 0.5) / source_size`. При недопустимой выборке обе координаты NaN. При выключенном запросе обе матрицы пусты, включая повторное использование inspection destination.

| Status | Значение |
|---|---|
| 0 `no_carrier` | Фон или перекрытие моделью автомобиля; различаются по carrier_regions |
| 1 `valid` | Кадр присутствует, проекция проходит геометрические ограничения |
| 2 `unavailable` | Нет входного кадра камеры |
| 3 `behind_camera` | Camera Z ≤ z_epsilon |
| 4 `outside_angle` | theta > theta_max |
| 5 `outside_image` | Центр выборки вне [0,width−1] × [0,height−1] |
| 6 `vehicle_footprint` | Участок пола, исключённый под кузовом |
| 7 `supplied_occluder` | Луч перекрыт явно заданным source_occlusion_box |

Приоритет: background/ego depth, footprint, отсутствие кадра, Z, угол, границы, optional supplied occluder. Проекция использует общую с основным shader функцию, а не повторный CPU расчёт. Значения получаются 12 дополнительными RGBA8 passes/readbacks (U, V, status для каждой камеры); сохраняемый payload — 36 bytes/output pixel, без transient readback/region buffers. Carrier XYZ запрашивается независимо. Внутренняя geometry policy всегда включает ego, даже если final weights/coverage view его не рисует. Поэтому допустимость hybrid sample сравнивается только вне background/ego.

`valid` означает прохождение настроенного supplied-box control, если он включён; это не означает отсутствие других перекрытий объектами сцены и не означает ненулевой вклад после seam/weight/pyramid policy. Это ещё не source-scene correspondence. API render-thread-only; сетевого продукта и клиентского метода пока нет. Стоимость captures не включена в завершённые до них regular stage timings; end-to-end render call становится дороже. Проверки и ограничения: [[validation/CARRIER_POSITIONS#Расширение: координаты исходных камер]].

### Opt-in fusion contributions

`fuse_research(samples, settings, FusionInspection*)` получает необязательный destination. `Renderer::render` включает тот же capture через третий флаг `RenderInspectionRequest::fusion_contributions`, результат — `RenderInspection::fusion`. Поддерживаются четыре hybrid research modes; запрос для `edge_feather`, `angular_feather`, `hard_best_angle` явно отклоняется до рендера. Без destination запрос также отклоняется. При отключённом флаге `fusion` отсутствует, повторный capture очищает прежнее состояние.

| Поле | Смысл / формат |
|---|---|
| `FusionResult::weights` | Существующие CV_32FC4 weights до optional seam smoothing; diagnostic weights view сохраняет этот смысл |
| `FusionInspection::blend_weights` | CV_32FC4 после smoothing/remasking/normalization, непосредственно перед pyramid reduction |
| `level_weights` | CV_32FC4 для каждого фактически построенного уровня, fine → coarse; сумма 1 при поддержке, иначе 0 |
| `level_contributions[level][4]` | Native-size CV_32FC3 signed weighted bands, fine→coarse, до reconstruction/clamp; nonmultiband имеет один level |
| `camera_contributions[4]` | Full-resolution CV_32FC3 signed linear RGB, восстановленный вклад камеры до финального clamp |

Для single-scale fusion вклад — произведение веса на цвет камеры. Для multiband это восстановление взвешенных Laplacian layers конкретной камеры; уровни могут иметь отрицательные значения, а spatial filtering переносит вклад через локальные границы validity. Сумма четырёх вкладов с последующим clamp [0,1] воспроизводит multiband color с погрешностью float regrouping. Эти вклады не являются вероятностями, opacity или покамерными локальными коэффициентами: final clamp и input-dependent seam solver не допускают такой интерпретации. При counterfactual изменении входа solver может выбрать другие швы.

Это данные fusion stage до server composition: ego overlay, fallback вне coverage и диагностические RGB views выполняются после него. Вклад за границей coverage не означает, что он попадёт в отправленный кадр. Geometry inspection отдельно depth-masks ego. Все capture matrices top-left; размеры уровней различаются, фактическое число может быть меньше pyramid_levels для малого raster.

Дополнительных GLES проходов не требуется. Сохраняемые contributions стоят 48 bytes/full-resolution pixel, blend weights — 16, level weights — 16 на пиксель каждого уровня; retained signed bands — ещё48 bytes на pixel каждого уровня. Bands удерживают reference-counted Mat buffers до освобождения capture; transient layers/reconstruction требуют дополнительной памяти. Без запроса эти diagnostic allocations не выполняются. При capture CPU затраты входят в fusion timer; сравнивать производительность с обычным режимом необходимо отдельным опытом. Wire delivery пока не реализован. Проверки: [[validation/CARRIER_POSITIONS#Расширение: вклад камер после смешивания]].

### Source-surface research probe

При BUILD_TESTING собирается `sv-correspondence-probe CONFIG MANIFEST OUTPUT`: один first-manifest-frame capture обычного native renderer, с carrier/source inspection, независимым запросом actual frames и проверкой RGBA parity до/после. Экспорт float32 little-endian с полями dimensions/channels/origin/hash в report.json; отсутствует camera scene truth внутри продукта. Blender ray procedure и read-only research collector сопоставляют source sample с scene surface отдельно. Команды, первый результат и ограничения: [[validation/SOURCE_CORRESPONDENCE]]. Wire/API сервера этот test utility не добавляет.

Signed band API и matched native validation: [[validation/HYBRID_CORRESPONDENCE]]. При выключенном capture новые bands не удерживаются; запрос не меняет fusion arithmetic. Native-level coefficients не являются full-resolution opacity, signed bands могут быть отрицательными. Для восстановления использовать native size chain с align-endpoint interpolation, затем sum/clamp перед encode/composition.

Rectangular support height: optional surface.reference_height_m, finite[0,5]m/default0, z=h+H(dx²+dy²)/2. CPU mesh и shader coordinates едины; region curvature считается относительно h, safe_view учитывает h+H. Только rectangular_bowl_v1; это supplied geometry assumption, не depth estimator. Контроль и границы: [[validation/REFERENCE_HEIGHT]].

Rectangular `surface.local_support_patch` — optional supplied local height field in vehicle coordinates. Required nested fields: `center_x_m`, `center_y_m`, `half_length_m`, `half_width_m`, `height_m`, `transition_m`. Half extents and transition ≥0.01m, height ∈[0,5]m; whole footprint including transition must fit within outer rectangular bounds. Omit the object to disable. The plateau adds `height_m`; outside the plateau the offset is `height_m * max(0, 1-max(dx,dy)/transition_m)`, where `dx=max(abs(x-center_x)-half_length,0)` and similarly for y. Outside this footprint the previous surface is unchanged. Mesh axes include plateau/transition edges; triangles linearly interpolate vertex heights (corner ramp creases need not coincide with every triangle). Conservative view clearance includes the patch height. Raised patch regions use diagnostic region4. This is an explicit geometry control; neither RGB depth estimation nor object visibility reconstruction is implemented by this parameter.

Rectangular `surface.source_occlusion_box` is an optional supplied opaque AABB in vehicle metres: `min_m:[x,y,z]`, `max_m:[x,y,z]`, `tolerance_m`. Finite ordered coordinates in[-100,100]m; tolerance[0,0.1]m. Omit to disable. The shader tests the finite camera-to-carrier segment against the box before source texture lookup; it rejects a source when the first intersection precedes the endpoint by more than tolerance. Camera origins are obtained from inverse camera extrinsics. It does not draw the box, change carrier XYZ or infer geometry from RGB; only this one known blocker is considered. Native GPU modes and hybrid sample validity share the predicate. Status7 `supplied_occluder` means rejection by this explicit control, UV becomes NaN. Surface updates also rebuild the mesh through the existing atomic path; visibility-only updates are not optimized. Vehicle-frame bounds must be updated for moving/rotating scenes; a world AABB transformed only by its min/max is insufficient under general rotation.

`source_occlusion_box.mode` is optional: `center` (default), `bilinear_radial_v1` or `bilinear_box_surface_v1` (finite-box preservation described below). Optional `guard_texels` is integer0/1, default0; center requires0. The footprint candidate always keeps the central segment rejection, then checks positive-coefficient bilinear texel-center rays (guard0) or a conservative4×4 texel neighborhood (guard1), clamped to source raster. Neighbour rays terminate at **the central carrier radial range**, not at independently observed scene depth. Any blocked ray rejects that camera sample with status7. This bounded candidate supports only zero-distortion equidistant cameras; schema rejects nonzero camera k rather than using a wrong inverse. Defaults preserve the center path; this is an experimental policy, not exact source filter/depth reconstruction. Guard1 is a spatial protection margin, not a calibrated Eevee kernel. Extra shader work and boundary erosion must be measured; rejection can remove useful samples.

Neighbour-ray tests use an interior inset of10µm on the supplied AABB to exclude exact edge/corner tangency from a float/double decision. The central ray retains the original closed-box rule. A box with no remaining interior contributes no neighbour rejection. This numerical convention arose in known-answer tests, before the research captures; it is distinct from the configurable metric endpoint tolerance.

Экспериментальный `bilinear_radial_v1` guard0/1 прошёл oracle/parity checks, но потерял большинство видимых target pixels в16-case pilot. Для обычной работы сохранён default `center`; результаты и команды независимого audit: [[validation/FILTER_VISIBILITY]].

## Supplied box surface preservation (experimental)

`surface.source_occlusion_box.mode="bilinear_box_surface_v1"` adds a bounded alternative to `bilinear_radial_v1`. After the unchanged central occlusion test, a carrier endpoint within the configured support band (default10µm, L∞ distance) of any finite supplied-box face retains the central sample. Hidden faces are still rejected. Points outside that surface band use the prior radial footprint test with `guard_texels=0/1`; zero-distortion equidistant optics are required. Interior points and face extensions farther than the numerical band receive no exemption. This recognizes one known box, not arbitrary scene surfaces or RGB depth. Default remains `center`.

The mode is selected through existing `Client::configure_surface` with the whole surface and current `base_config_revision`; the server validates and publishes it between renders. `surface_catalog.rectangular_source_occlusion.modes` lists it, and `default_support_tolerance_m=0.00001` and `support_tolerance_range_m=[0,0.05]` describe the optional configurable `support_tolerance_m`. It is separate from configurable `tolerance_m` (central endpoint bias); clients must not submit the catalog metadata as config fields. No new message/transport is introduced. A GUI control for this mode is still pending.

24-condition box-surface pilot улучшает target preservation до75/76 и69/69 при outside1/0, но strict gate первого кадра не пройден; default center сохранён. Сравнение и воспроизведение: [[validation/BOX_SURFACE_VISIBILITY]].

`source_occlusion_box.support_tolerance_m` is optional and valid only with `mode=bilinear_box_surface_v1`: finite value in[0,0.05]m, default0.00001m. It changes support membership alone; it does not expand the occluder, alter `tolerance_m`, remove central-ray rejection or change neighbor inset. A larger band explicitly assumes more geometry uncertainty and can exempt incorrect surfaces. The old fixed-band catalog key is replaced by the default/range keys. Omitted field preserves previous pixels; other modes reject the field to avoid silent no-op configuration. Apply and restore via existing full-surface `Client::configure_surface` and revision checks.

76-condition sensitivity и команды повторения: [[validation/VISIBILITY_SENSITIVITY]]. Nominal support_tolerance_m=0.001/tolerance_m=0.02/guard1 сохраняет76/76 и69/69, но robustness к±5mm Z bounds error не подтверждена. Это developmental profile, default center не изменён.


### Native маска локальной поддержки

`RenderInspectionRequest::support=true` возвращает `RenderInspection::support` (`render_support_v1`). Это опциональный результат C++ renderer; сервер пока не экспортирует его по SV01 и `sv-client-lib` не объявляет соответствующую capability. Пример вызова:

```cpp
sv::RenderInspection diagnostic;
sv::RenderInspectionRequest request;
request.support = true;
auto rgba = renderer.render(frames, view, &diagnostic, request);
const cv::Mat &mask = diagnostic.support;
```

Маска CV_8UC4 имеет размеры final RGBA и top-left origin. Здесь порядок каналов **RGBA по контракту**, не обычный BGR OpenCV. Это integer data, а не цвет: визуализировать нужно через явную палитру. Без запроса матрица пустая, включая повторное использование destination.

| Канал | Значение |
|---|---|
| R | Eligible camera count, 0–4; projection/availability/source-occlusion checks |
| G | Positive local weight count, 0–4; для hard_best_angle выбранная камера даёт 1 |
| B | RenderSupportPath: 0 background, 1 vehicle_footprint, 2 vehicle_model, 3 gpu_fallback, 4 gpu_fusion, 5 hybrid_fallback, 6 hybrid_fusion |
| A | 0 background; 255 для depth-tested geometry |

На background/ego R=G=0. `vehicle_footprint` — procedural footprint rectangular carrier; `vehicle_model` — видимая depth-tested модель. Запрос без destination или при `fusion.diagnostic != color` отклоняется `invalid_argument`: depth/ego semantics диагностических RGB views здесь не смешиваются с final color.

GLES проход использует тот же `source_projection` и формулы весов, что normal render, с отключённым dithering; RGB samples не считываются. Для weighted GPU modes G считает строго положительные веса **до** проверки суммы `>1e-6`. Поэтому G>0 ещё допускает B=gpu_fallback. Hard mode выбирает одну eligible камеру и при нулевом angle score. Hybrid counts берутся из actual sample validity и `FusionResult::blend_weights` после seam smoothing, до pyramid reduction; `weights` остаются прежними decision weights. Hybrid composition выбирает fusion по eligibility count, поэтому B=hybrid_fusion допустим при G=0. B описывает фактическую ветку, не гарантированную поддержку цветом.

Маска занимает 4 bytes/output pixel; hybrid временно хранит ещё 4 bytes/pixel для counts. Относительно существующей region inspection добавляется один RGBA8 pass/readback; запрос только support всё равно выполняет region pass, то есть всего два прохода после normal render. Readback требует временного RGBA8 буфера. Возвращаемые actual blend weights удерживают reference-counted 16 bytes/pixel уже вычисленной матрицы, без её копирования и без allocations signed bands. `RenderTiming::inspection_cpu_ms` измеряет geometry inspection/readbacks и merge; заполнение hybrid counts входит в `fusion_cpu_ms`. Это CPU wall time, не отдельный GPU timer; поле пока не передаётся сервером. Cost при полном resolution следует измерить отдельно.

Ни R, ни G не являются scene observability/correspondence. Для multiband локальные base weights не описывают spatial/signed contribution конечного пикселя. Формат не меняет fallback policy и не объявляет reconstruction наблюдением. Known-answer проверки и команды: [[validation/RENDER_SUPPORT]].
