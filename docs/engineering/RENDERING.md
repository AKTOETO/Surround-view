# Поверхности, слияние и диагностика рендера

Реализовано в Linux-профиле 0.4.0. Связи: [[engineering/USAGE]], [[research/PROJECTION_AND_STITCHING]], [[validation/SURFACE_SCREENING]], [[prototype/STATUS]]. Калибровка и исходные четыре кадра остаются общими для вариантов. Сервер использует прямую проекцию на поверхность; предварительная panorama/cubemap-текстура не создаётся.

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

При отсутствии блока сохраняется прежнее `edge_feather`, `color`, ширина 24 px и степень 2. Внутри блока обязательна только `mode`. Неизвестные ключи/значения отвергаются. Ширина должна быть конечной и находиться в (0,4096], степень — в (0,32]. Эти параметры выбираются при запуске; переключения fusion командой клиента пока нет.

Для доступной камеры проверяются глубина, угол fisheye и границы изображения. Пусть `e` — минимальное расстояние проекции до четырёх краёв кадра, `theta` — угол относительно оптической оси. Краевой вес `b = clamp(e / edge_width_px, 0, 1)`, угловой вес `a = cos(theta)^angle_power`.

| Режим | Выбор/вес | Ограничение |
|---|---|---|
| `edge_feather` | Вес b, нормированная сумма | Усреднение не исправляет параллакс |
| `angular_feather` | Вес b × a | Предпочтение оси камеры — эвристика, не измеренная карта качества |
| `hard_best_angle` | Единственная валидная камера с максимальным a | Резкий шов; при равенстве выбирается меньший ID |

Два feather-режима декодируют выбранные цвета из sRGB, смешивают в linear RGB и кодируют обратно. Если сумма весов ≤1e-6, используется непрозрачная заливка. Аппаратная фильтрация RGB8-текстур выполняется до декодирования: это не полностью линейная интерполяция источника. Hard-режим выводит выбранную выборку без межкамерного усреднения. Graph-cut, multi-band и компенсация экспозиции ещё не реализованы.

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
