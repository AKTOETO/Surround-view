# E-CAL-capture-01: размер доски и наклон как отдельные факторы

Дата: 10.10.2026. Продолжение [[CALIBRATION_COVERAGE]], частичное выполнение этапа 3 [[planning/ROADMAP]]. [Протокол](../research/CALIBRATION_CAPTURE_PROTOCOL.md) зафиксирован коммитом `28422a9` **до detector/solver исходов основной серии**. Геометрические диапазоны выбраны после clipping check; исследование exploratory, не confirmatory holdout. Thresholds по результатам не менялись.

## Что сравнивается

Четыре прежние radial optical families: equidistant, KB с ненулевыми коэффициентами, equisolid, stereographic. Формулы и known-geometry oracle — [[OPTICAL_FAMILY_CALIBRATION]], математическая основа — [Kannala–Brandt](https://users.aalto.fi/~kannalj1/calibration/Kannala_Brandt_calibration.pdf). Production detector и solver реально исполняются в C++ OpenCV 5.0.0, без Python cv2. Оба solver profiles остаются [OpenCV fisheye polynomial](https://docs.opencv.org/4.13.0/db/d58/group__calib3d__fisheye.html), order=2/4.

Seeds 7101/7102. Четыре одинаковых центральных train views дополнены восемью периферийными. Их центры имеют θ=0.65/0.8 rad в четырёх азимутальных секторах; directions, roll и порядок совпадают во всех профилях одного seed. Факторы:

| Profile | Distance, m | Local x/y tilt | Интерпретация |
|---|---:|---:|---|
| small_front | 3.2 | 0 | Малое изображение, normal вдоль center ray |
| small_tilt | 3.2 | ±0.35 rad | Малое изображение, чередующийся двухосевой наклон |
| large_front | 2.0 | 0 | Крупное изображение, normal вдоль center ray |
| large_tilt | 2.0 | ±0.35 rad | Крупное изображение, чередующийся двухосевой наклон |

Знаки local rotations фиксированы по индексу view. При tilt=0.35 rad фактический угол normal-to-center-ray равен $\arccos(\cos^2(0.35))\approx0.490$ rad, а не 0.35 rad: это последовательность двух поворотов. Distance меняет масштаб и angular extent; tilt — foreshortening и corner occupancy. **Направления центров фиксированы, направления отдельных corners не фиксированы.** Поэтому это controlled geometric-factor comparison, не чистый image-resize или полностью постоянное θ histogram.

Actual train corner θ max≈0.917/0.918 rad у small front/tilt и ≈0.985–0.989 rad у large front/tilt. Полное 2D покрытие не обеспечено; image corners и непрерывные азимутальные направления не исследованы. Отдельный переход 0.9→0.8 rad относительно прежнего опыта означает, что межсерийные числа нельзя считать парными.

![Парные растры четырёх профилей](../diploma/figures/experiments/calibration_capture_views.png)

*Рисунок 1 — Фактические PNG kb_nonzero/7101/view04 при одном center direction. Подписи размера ячейки — median соседних рёбер внутренней сетки по истинной проекции по всем восьми периферийным train views, а не по одному изображению. Размер вычислен из истинной проекции, не из detector output.*

## Split, gate и метрики

Во всех профилях ровно 12 train views. Общая validation содержит 4 central views и 8 новых peripheral poses: seed+2000, distance=2.6 м, tilt=±0.175 rad. Train peripheral seed=base+1000; центральные train/validation — различные poses одного процедурного seed. Content hashes train/validation не пересекаются. Это синтетический disjoint split, не доказательство физической независимости соседних кадров.

Всего **384 уникальных PNG, 384/384 detector successes, 128 calibration attempts**: 64 сочетания family/seed/profile/order, каждое с central gate (4 validation views) и full gate (12 views). Ни один input/fit не отброшен или заменён. Оба gate экспортировали 64/64 models. Порог p95≤1 px и monotonicity на [0,1] rad сохранены. Full residual p95 находится в диапазоне 0.2602–0.3250 px. В отличие от предыдущей серии все inner train/validation corners находятся при θ<1 rad; maximum peripheral validation θ=0.944972 rad.

Два CLI вызова повторно оценивают одни train images. Все 64 пары экспортированных estimates совпадают по fx/fy/cx/cy/k точно: max parameter delta=0. На central-gate estimates вычисляются общие oracle metrics:

- **Outer ray p95:** прежние контрольные лучи θ∈[0.7,1], истинно видимые в кадре; estimated bad projections не исключаются.
- **Floor p95:** true UV точек известной плоскости Y=1.2 м инвертируются estimated model; X∈[−3,3], Z∈[1,8], θ<0.95. Камера/плоскость считаются идеально известными; invalid points=0 во всех 64 моделях.
- **Fixed-pose p95:** все 12 validation board poses фиксированы истинными, всего 648 углов; это отличается от прежней серии, где fixed-pose metric включала только 4 central views. Напрямую сравнивать эти два fixed-pose summary нельзя.
- Сохраняются detector localization, cell-edge sizes, normal-to-ray tilt, coverage histogram и оба pose-fitted residual.

Известные позы/контрольные точки не участвуют в fit intrinsics. Реализация optical equations общая для части generator/oracle, поэтому это проверка synthetic geometry, не полностью независимая физическая метрология.

## Все результаты

| Family | Seed | Profile | Order | Full gate p95, px | Fixed-pose p95, px | Outer p95, px | Floor p95, m |
|---|---:|---|---:|---:|---:|---:|---:|
| equidistant | 7101 | small_front | 2 | 0.2881 | 2.3277 | 2.3387 | 0.0586 |
| equidistant | 7101 | small_front | 4 | 0.3039 | 1.6515 | 1.7584 | 0.1213 |
| equidistant | 7101 | small_tilt | 2 | 0.3087 | 3.8634 | 3.9001 | 0.0773 |
| equidistant | 7101 | small_tilt | 4 | 0.3110 | 3.7775 | 3.8706 | 0.0511 |
| equidistant | 7101 | large_front | 2 | 0.2805 | 1.5841 | 1.7399 | 0.0190 |
| equidistant | 7101 | large_front | 4 | 0.2826 | 1.1721 | 1.2958 | 0.0239 |
| equidistant | 7101 | large_tilt | 2 | 0.2832 | 1.6232 | 1.7821 | 0.0196 |
| equidistant | 7101 | large_tilt | 4 | 0.2856 | 1.4963 | 1.6717 | 0.0245 |
| equidistant | 7102 | small_front | 2 | 0.2755 | 0.9274 | 1.0700 | 0.0737 |
| equidistant | 7102 | small_front | 4 | 0.2765 | 0.9023 | 1.5531 | 0.0678 |
| equidistant | 7102 | small_tilt | 2 | 0.2821 | 1.8858 | 1.9303 | 0.0377 |
| equidistant | 7102 | small_tilt | 4 | 0.3040 | 2.9635 | 3.0620 | 0.0446 |
| equidistant | 7102 | large_front | 2 | 0.2766 | 0.4397 | 0.4704 | 0.0203 |
| equidistant | 7102 | large_front | 4 | 0.2775 | 0.2671 | 0.2685 | 0.0125 |
| equidistant | 7102 | large_tilt | 2 | 0.2813 | 2.8377 | 3.0633 | 0.0796 |
| equidistant | 7102 | large_tilt | 4 | 0.2762 | 3.1915 | 3.4625 | 0.0780 |
| kb_nonzero | 7101 | small_front | 2 | 0.2796 | 1.1578 | 1.1565 | 0.0669 |
| kb_nonzero | 7101 | small_front | 4 | 0.2830 | 1.3934 | 1.4843 | 0.0241 |
| kb_nonzero | 7101 | small_tilt | 2 | 0.2686 | 1.2711 | 1.7377 | 0.0717 |
| kb_nonzero | 7101 | small_tilt | 4 | 0.2661 | 1.1695 | 1.6816 | 0.0699 |
| kb_nonzero | 7101 | large_front | 2 | 0.2753 | 1.0691 | 1.1061 | 0.1551 |
| kb_nonzero | 7101 | large_front | 4 | 0.2743 | 1.1344 | 1.1951 | 0.1369 |
| kb_nonzero | 7101 | large_tilt | 2 | 0.2718 | 0.7757 | 0.8291 | 0.0436 |
| kb_nonzero | 7101 | large_tilt | 4 | 0.2696 | 0.9570 | 1.0262 | 0.0311 |
| kb_nonzero | 7102 | small_front | 2 | 0.2602 | 1.2462 | 1.1977 | 0.0751 |
| kb_nonzero | 7102 | small_front | 4 | 0.2654 | 1.3399 | 1.5668 | 0.0771 |
| kb_nonzero | 7102 | small_tilt | 2 | 0.2659 | 2.2188 | 2.1922 | 0.0615 |
| kb_nonzero | 7102 | small_tilt | 4 | 0.2641 | 2.5054 | 2.5674 | 0.0405 |
| kb_nonzero | 7102 | large_front | 2 | 0.2639 | 1.6150 | 1.6991 | 0.0541 |
| kb_nonzero | 7102 | large_front | 4 | 0.2672 | 1.9106 | 2.0565 | 0.0443 |
| kb_nonzero | 7102 | large_tilt | 2 | 0.2670 | 1.3857 | 1.5559 | 0.0248 |
| kb_nonzero | 7102 | large_tilt | 4 | 0.2706 | 1.1240 | 1.3324 | 0.0163 |
| equisolid | 7101 | small_front | 2 | 0.3157 | 1.4428 | 1.4921 | 0.0496 |
| equisolid | 7101 | small_front | 4 | 0.3102 | 1.1512 | 1.4514 | 0.0730 |
| equisolid | 7101 | small_tilt | 2 | 0.3194 | 2.0524 | 2.1204 | 0.0521 |
| equisolid | 7101 | small_tilt | 4 | 0.3246 | 2.1044 | 2.1728 | 0.0448 |
| equisolid | 7101 | large_front | 2 | 0.3086 | 1.5497 | 1.7193 | 0.0245 |
| equisolid | 7101 | large_front | 4 | 0.3094 | 1.3107 | 1.4866 | 0.0185 |
| equisolid | 7101 | large_tilt | 2 | 0.3078 | 1.3336 | 1.5561 | 0.0552 |
| equisolid | 7101 | large_tilt | 4 | 0.3084 | 1.0713 | 1.2780 | 0.0821 |
| equisolid | 7102 | small_front | 2 | 0.3231 | 1.4317 | 1.6751 | 0.0659 |
| equisolid | 7102 | small_front | 4 | 0.3208 | 1.2514 | 1.2648 | 0.0528 |
| equisolid | 7102 | small_tilt | 2 | 0.3199 | 1.1463 | 1.3060 | 0.0656 |
| equisolid | 7102 | small_tilt | 4 | 0.3236 | 1.4059 | 1.5048 | 0.0736 |
| equisolid | 7102 | large_front | 2 | 0.3250 | 0.9130 | 0.9440 | 0.0850 |
| equisolid | 7102 | large_front | 4 | 0.3235 | 1.1977 | 1.2307 | 0.0704 |
| equisolid | 7102 | large_tilt | 2 | 0.3232 | 2.6267 | 2.7119 | 0.1308 |
| equisolid | 7102 | large_tilt | 4 | 0.3230 | 2.8074 | 2.9059 | 0.1340 |
| stereographic | 7101 | small_front | 2 | 0.2735 | 1.8134 | 1.8251 | 0.0777 |
| stereographic | 7101 | small_front | 4 | 0.2773 | 0.7424 | 0.8594 | 0.0270 |
| stereographic | 7101 | small_tilt | 2 | 0.2740 | 2.5600 | 2.8785 | 0.0371 |
| stereographic | 7101 | small_tilt | 4 | 0.2786 | 2.1143 | 2.3805 | 0.0494 |
| stereographic | 7101 | large_front | 2 | 0.2724 | 1.1031 | 1.1662 | 0.0988 |
| stereographic | 7101 | large_front | 4 | 0.2754 | 1.1920 | 1.2968 | 0.0413 |
| stereographic | 7101 | large_tilt | 2 | 0.2671 | 1.3383 | 1.4741 | 0.1308 |
| stereographic | 7101 | large_tilt | 4 | 0.2673 | 1.3538 | 1.4638 | 0.1024 |
| stereographic | 7102 | small_front | 2 | 0.2647 | 2.8055 | 2.8082 | 0.1243 |
| stereographic | 7102 | small_front | 4 | 0.2659 | 2.6257 | 3.1500 | 0.1195 |
| stereographic | 7102 | small_tilt | 2 | 0.2655 | 1.2327 | 1.4779 | 0.0504 |
| stereographic | 7102 | small_tilt | 4 | 0.2695 | 1.3503 | 1.3948 | 0.0439 |
| stereographic | 7102 | large_front | 2 | 0.2694 | 3.2588 | 3.4814 | 0.0862 |
| stereographic | 7102 | large_front | 4 | 0.2691 | 3.2744 | 3.4966 | 0.0883 |
| stereographic | 7102 | large_tilt | 2 | 0.2693 | 3.0680 | 3.3035 | 0.0735 |
| stereographic | 7102 | large_tilt | 4 | 0.2710 | 2.8952 | 3.1371 | 0.0675 |

![Ошибки четырёх профилей на общих точках](../diploma/figures/experiments/calibration_capture_metrics.png)

*Рисунок 2 — Outer ray/floor p95 отдельно по family, seed и order. Все 64 central-gate estimates доступны; отсутствующие модели не заменялись нулями. Четыре столбца одного case используют одну контрольную геометрию.*

## Парные эффекты и пределы вывода

Улучшением считается строгое уменьшение отдельного p95 внутри пары. В каждой строке восемь family/seed cases; counts описательные, не вероятности успеха и не независимые физические trials.

| Переход | Order | Outer p95 уменьшился | Floor p95 уменьшился |
|---|---:|---:|---:|
| small→large, front | 2 | 5/8 | 5/8 |
| small→large, front | 4 | 4/8 | 5/8 |
| small→large, tilt | 2 | 5/8 | 3/8 |
| small→large, tilt | 4 | 5/8 | 3/8 |
| front→tilt, small | 2 | 2/8 | 5/8 |
| front→tilt, small | 4 | 1/8 | 5/8 |
| front→tilt, large | 2 | 4/8 | 3/8 |
| front→tilt, large | 4 | 4/8 | 3/8 |

Контрпример **kb_nonzero/7101/order4**: увеличение фронтальной доски снижает outer p95 с 1.4843 до 1.1951 px, но floor p95 растёт с 0.0241 до 0.1369 м. Оба full residual близки: 0.2830 и 0.2743 px (точные значения — frozen report). Положительный эффект по одной метрике не гарантирует положительного эффекта по другой.

Контрпример **equidistant/7102/order4**: large_front имеет outer p95≈0.2685 px, floor p95≈0.0125 м; large_tilt — ≈3.4625 px и ≈0.0780 м. Конкретное чередование наклонов не улучшает результат универсально. Это не опровергает пользу разнообразных поз вообще: исследованы лишь два pose seeds и один tilt pattern; solver conditioning/uncertainty и направленный detector bias не измерены. Приписывать ухудшение только detector или только solver нельзя.

Все модели accepted, однако максимальный floor p95≈0.1551 м, fixed-pose p95≈3.8634 px. Полный pose-fitted residual остаётся <0.325 px. При идеально известных extrinsics это подтверждает ограничение данного gate, но без физического acceptance requirement не определяет false-accept rate. Из опыта нельзя получить универсальные безопасные расстояния/наклоны или повысить/понизить threshold.

Следующая диагностика: отделить detector localization bias от чувствительности solver к позам, проверить blur/noise при том же paired design, затем nonradial/extrinsic/ground-height perturbations. Для итоговой подтверждающей серии потребуются заранее заданные физические допуски, новые scene/acquisition units и физические снимки. Исторические exploratory данные не становятся holdout после записи отчёта.

## Воспроизведение

```sh
cmake -S . -B build
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py compare-capture --output artifacts/calibration-capture-repeat --seeds 7101 7102
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_capture.py --results artifacts/calibration-capture-repeat
ctest --test-dir build -R 'calibration_capture_factors|calibration_coverage|optical_models|raster_calibration' --output-on-failure
```

Output новый; Python NumPy/SciPy/Pillow/Matplotlib, C++ OpenCV; Blender/Python cv2 не нужны. [Frozen summary](baselines/calibration_capture_v1.json) содержит все estimates/reports/process outputs, geometry, diagnostics и hashes. Проверены 384 уникальных image hashes, 64 dataset hashes, train/validation content disjointness, одинаковая validation во всех профилях, равный budget, source/binary hashes. Сценарий сравнивает hashes исходников и binary до/после серии и отказывается выпускать summary при их изменении. Для исторического восстановления hashes нужна соответствующая версия Git и toolchain; разные OpenCV версии могут изменить detections/fits. Raw PNG/outputs сохраняются в `artifacts/` и воспроизводятся кодом.

6 новых regression tests проверяют center-direction invariance, rotation/analytic normal tilt, неизменность rotation при distance, видимость outer border corners/отсутствие pose overlap, projected cell size, ошибочные параметры и production detection на large tilted kb_nonzero PNG. Все 9 выбранных CTest suites прошли: `calibration_job`, `vision_tools`, `optical_models`, `calibration_coverage`, `calibration_capture_factors`, `raster_calibration`, `calibration`, `real_data_calibration`, `calibration_observations`.
