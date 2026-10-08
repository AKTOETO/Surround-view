# Blender depth truth: smoke and analytic validation

Дата: 09.10.2026. Статус: EXR → fisheye radial-range export smoke-tested; geometry conversion independently checked against analytic planes; object-ID and discontinuity validation remain open.

## Формат и происхождение

Blender сохраняет OpenEXR float Z для пяти cube faces каждой камеры (`nz` пропускается при FOV < 180°). Конвертер `tools/blender/convert.py` преобразует camera-axis Z в расстояние по fisheye-лучу, записывает `float32 NPY`; `NaN` означает отсутствие видимой поверхности. SHA-256 входов и выходов заносится в capture/replay metadata. Камеры и координаты определены в [[engineering/BLENDER]].

Первый capture: Blender 5.2.2 LTS / EEVEE, `blender_metric_street_v1`, четыре разнесённых optical centers, один кадр, 32×32 texels на cube face. Он содержит 20 EXR и четыре radial-depth карты. В долях валидных fisheye-пикселей: камера 0 — 52.44%, 1 — 53.22%, 2 — 52.24%, 3 — 53.21%; median depth 2.242–3.012 м, p95 16.233–17.355 м, максимум 30.500–37.050 м. Эти значения описывают только покрытие и геометрию данной сцены.

## Независимая проверка cube-depth → radial range

Добавлен `tools/validate_depth_plane.py`. Он строит camera-Z cube-face карты по аналитическому пересечению луча с плоскостью, используя независимые явные направления шести cube faces, затем пропускает эти карты через production depth converter. Для unit normal `n`, расстояния плоскости `q` и единичного fisheye луча `d` oracle range равен

$$
r = \frac{q}{n\cdot d}, \qquad n\cdot d>0.
$$

Для каждого cube face axial depth вычисляется по его собственному optical forward `f_face`: `Z_face = r (d·f_face)`. Это важно: Z на боковой cube-грани не является Z исходной fisheye камеры. Проверены фронтальная и наклонная плоскости, расстояния 2/6/20 м, face size 32/64/128/256. Первичная статистика ограничивает range 30 м; report отдельно содержит диапазоны по глубине и углу.

```sh
python3 tools/validate_depth_plane.py \
  --output artifacts/depth-plane-validation-v2 \
  --sizes 32 64 128 256
python3 tests/test_blender_fixture.py
```

На центральной зоне `θ≤0.6 rad` для наклонной плоскости 6 м face size 128 дал p95 radial error около 0.000019 м, при face size 256 — около 0.000005 м. Для фронтальной плоскости на дальности 2 м общий p95 по области `range≤30 m` уменьшился с 1.794 м (32×32) до 0.208 м (256×256). Большая ошибка на периферии ожидаема для почти касательных лучей и резче растёт с глубиной; поэтому единый максимальный порог без разбивки по углу/дальности вводил бы в заблуждение. Unit test проверяет 100% валидность сравниваемых лучей, сходимость при росте face size и p95 <0.1 мм для наклонной плоскости в центральном ROI.

Первичный JSON/Markdown остаётся в локальном игнорируемом каталоге `artifacts/depth-plane-validation-v2/`; точное повторение — команда выше. Этот аналитический тест изолирует оптическую геометрию и билинейную дискретизацию; он не выполняет Blender rasterization, EXR decoding, occlusion boundary или object-ID проверку.

## Depth-based visibility screening для E-STITCH-01

На имеющемся RGB/depth наборе запущен `tools/compare_visibility.py`: пять carriers × два виртуальных ракурса × три direct fusion modes (30 случаев). Для carrier point `X` источник считается точно видимым, если

$$
|d_i(\pi_i(X))-\|T_iX\|| \le \max(0.05\,\text{m},\ 0.01\|T_iX\|).
$$

Скрипт отдельно считает проекционную coverage, coverage точек, подтверждённых хотя бы одной глубиной, доли fusion weight от совпадающей глубины/ближнего foreground occluder/первой поверхности за точкой/no-return, и долю пикселей, где camera с максимальным весом depth-consistent. Использованы один синхронный кадр и его RGBA/калибровки/глубины; все файлы проверяются по SHA-256. Полная методика и выводы: [[validation/STITCH_VISIBILITY]], обзор: [[research/PROJECTION_AND_STITCHING]].

```sh
python3 tools/compare_visibility.py \
  --dataset artifacts/blender-depth-truth-dataset-fixed \
  --output artifacts/stitch-visibility-v3
```

Измеренный купол в low-view имеет 99.94% projection coverage, но только 16.20% depth-exact point coverage. Суммарный edge-feather вес на совпадающие по depth выборки — 13.18%, у hard-best-angle — 14.05%. Это показывает, что valid fisheye projection не равна физической видимости точки поверхности. Значения не измеряют эстетический seam или ghosting: выбранная поверхность условна, depth face низкого разрешения, а сравнение выполнено на одном статическом synthetic кадре.

## Что эти проверки подтверждают и чего не подтверждают

Они подтверждают преобразование непрерывной геометрии для фронтальных/наклонных плоскостей в заданных диапазонах и дают воспроизводимый первый тест связи projection/fusion с известной глубиной сцены. Не подтверждены точность Blender EXR rasterization на независимой сцене, sphere/cube geometry, discontinuous silhouettes, semantic/object-ID visibility, кузов/собственная окклюзия камеры, seam ΔE/gradient, число ghost contours и temporal seam stability. Depth-aware quality metrics — только один слой исследования и не подменяют image-based независимый эталон.
