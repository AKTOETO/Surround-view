# Чувствительность изображения к плотности сетки

11.10.2026. Frozen master: `configs/research/carrier-refinement-plan.json`, протокол [[research/CARRIER_REFINEMENT_PROTOCOL]]. Compact [carrier_refinement_v1.json](baselines/carrier_refinement_v1.json) содержит360 condition metrics,120 paired refinement rows, resources, input/config/native-report hashes и provenance. Raw reports/RGBA: `artifacts/carrier-refinement-v1/{coarse,fine}`, medium: `artifacts/carrier-budget-v1`. Прежний medium baseline закреплён SHA256, не перезаписан.

## План и фактические ресурсы

Изменены только cells, physical shape каждого carrier сохранена. Общие cameras/poses/RGB/truth/visibility и четыре fusion profiles неизменны. Seeds101–103 и medium результаты уже просмотрены: exploratory follow-up, не holdout. Coarse/fine budgets не равны между разными carriers; сравнивается каждый carrier с самим собой.

| Носитель | Triangles coarse / medium / fine | Position/index bytes coarse / medium / fine |
|---|---:|---:|
| plane | 864 / 3196 / 12144 | 16068 / 58512 / 220500 |
| bowl | 864 / 3196 / 12144 | 16068 / 58512 / 220500 |
| dome_floor_v1 | 832 / 3200 / 13056 | 15384 / 58392 / 236568 |
| cylinder_floor_v1 | 768 / 3200 / 13056 | 14616 / 59160 / 238104 |
| cube_floor_v1 | 840 / 3208 / 12552 | 16392 / 60168 / 230664 |

Fine — конечный reference для sensitivity, не scene truth или доказательство асимптотической сходимости. Плотный flat floor не восстанавливает scene depth. Output320×180, inputs400×400, два кадра/четыре profiles на три street instances; нет high-angle views, long clips или нового scene family.

Audited360 условий,3240 samples с warmup и2520 measurement RGBA. Новые coarse/fine:240 условий,2160 samples,1680 measurement RGBA на30 fresh servers. Medium120 условий/15 servers повторно audited: source fingerprint и native/config/input/capture hashes совпали с pinned baseline. Все success/restore; active=resident, monotonic cells/resources и ceilings проверены. Cursor/history не восстанавливаются. Во время новых timed runs не выполнялись build/CTest/Blender capture. Fixed level/carrier order и разное время получения medium исключают speed/thermal ranking.

Source fingerprint всех уровней: `1d5e030c8dab6b732c05b80be06ea3a5514b5e7b3accee38383f33b0f2cf4845`. Product C++ не менялся; использована прежняя сборка [[validation/CARRIER_BUDGET]]. Compact baseline не дублирует full native metadata; для полного re-audit нужны hash-verified локальные raw reports/RGBA. Новое полное воспроизведение требует новой frozen версии plan/baseline: timings/session IDs не битово воспроизводимы.

## Две разные оценки

1. Difference с finite fine output: mean absolute linear RGB difference внутри общей any-camera ROI, full-frame/ROI changed-pixel fractions, max RGB8 channel delta. Это sensitivity к mesh, не scene error.
2. Ошибка относительно independent direct Blender truth: signed Δ RGB MAE и coded IoU для medium−coarse/fine−medium. Эти значения не обязаны монотонно улучшаться при сгущении surrogate mesh.

| Носитель | Linear difference coarse→fine | Linear difference medium→fine | Max medium/fine RGB8 delta в ROI | Medium ближе к fine |
|---|---:|---:|---:|---:|
| plane | 3.144e-07…2.267e-06 | 2.372e-07…6.201e-07 | 2 | 22/24 |
| bowl | 6.641e-04…8.501e-04 | 1.469e-04…2.022e-04 | 72 | 24/24 |
| dome_floor_v1 | 3.957e-06…1.118e-05 | 1.424e-06…5.296e-06 | 6 | 24/24 |
| cylinder_floor_v1 | 1.254e-06…5.159e-06 | 5.119e-07…2.163e-06 | 2 | 24/24 |
| cube_floor_v1 | 2.997e-07…7.840e-07 | 1.396e-07…6.015e-07 | 1 | 24/24 |

Диапазоны охватывают24 scene/frame/profile conditions на carrier, не доверительные интервалы. Medium ближе к fine в118/120 пар, два исключения — plane. Ни один coarse/fine или medium/fine output не совпал полностью RGBA byte-for-byte. Plane/cube differences малы, но происхождение raster/float/interpolation effects отдельно не локализовано.

Bowl наиболее чувствителен: medium/fine linear difference1.469e−4…2.022e−4;8.286…10.086% ROI pixels отличаются, max channel delta72/255. Для остальных carriers max medium/fine RGB8 delta: plane2, dome6, cylinder2, cube1. Среднее не ограничивает локальные силуэтные ошибки. Вытягивание поднятого target сохраняется на всех уровнях.

| Носитель | Δ truth RGB MAE fine−medium | RGB лучше / хуже | IoU лучше / хуже |
|---|---:|---:|---:|
| plane | -1.091e-07…+7.516e-08 | 8 / 16 | 0 / 0 |
| bowl | -1.130e-04…-4.833e-05 | 24 / 0 | 3 / 8 |
| dome_floor_v1 | -2.905e-06…-8.823e-07 | 24 / 0 | 2 / 0 |
| cylinder_floor_v1 | -1.424e-06…-6.596e-08 | 24 / 0 | 2 / 0 |
| cube_floor_v1 | -2.556e-07…+1.792e-07 | 12 / 12 | 0 / 2 |

Fine уменьшает direct-truth RGB MAE в92/120 условий и увеличивает в28. Coded IoU улучшается в7, ухудшается в10, неизменна в103. Bowl улучшает RGB MAE во24/24 парах, но ухудшает IoU в8. Сгущение mesh не является достаточным условием лучшей silhouette или физически правильной геометрии.

## Проверки и следующие этапы

Known-answer Python controls: opaque RGBA8, boolean nonempty ROI, exact zero, black/white linear difference0.5, changed fractions и max delta255. Errors вне ROI исключены из ROI metric. Negative controls отклоняют shape change, non-increasing/укороченные cell axes и изменённый output. Targeted CTest object_metrics прошёл после изменений; последняя full native regression51/51 описана в [[validation/CARRIER_BUDGET]], повторная сборка product кода не требовалась.

Остаются ground/raised/shell ROI, spatial triangle coverage/density, высокие/боковые views, input/output-resolution sensitivity, другие scene families/long clips и реальные target budgets. Acceptance threshold для автомобиля не задан и здесь не придумывается. Серия не заменяет physical/Aurora test.
