# Scene strata и границы: ограничения общей RGB-ошибки

11.10.2026. Протокол [[research/SPATIAL_ROI_PROTOCOL]], plan `configs/research/spatial-roi-plan.json`; compact [spatial_roi_v1.json](baselines/spatial_roi_v1.json). Это post-hoc анализ уже просмотренных360 outputs предыдущего [[validation/CARRIER_REFINEMENT]], не новые server trials/holdout. Исходный baseline закреплён SHA256, все raw reports/inputs/RGBA повторно проверены. Product C++ не изменялся.

## Независимая разметка

Hash-verified direct Blender ray-cast object-ID + any-camera visibility определяют шесть непересекающихся masks. Ground — road/ground/разметка по заранее фиксированным object names и suffix `.NNN`; coded_target — отдельный diagnostic ID; other_scene — остальные non-ego objects. Background/ego исключаются. Ground означает семантическую дорогу, не измеренную высоту z=0. Other_scene включает curb/sidewalk/buildings/traffic objects, не строго raised-class.

Для каждой группы разделены interior и boundary band. Band повторяет прежнее исключение: max-filter3×3 IDs != min-filter3×3, dilation2. Это границы всех объектов/маркировок, не только силуэты. Union interiors побитно равен старой evaluation ROI; union всех шести masks — весь independently visible non-ego/nonbackground scene. RGB output не участвует в построении ROI. Mask hashes/counts сохранены на всех шести input frames.

| Seed / кадр | Ground interior | Other interior | Target interior | Все boundary pixels |
|---|---:|---:|---:|---:|
| 101 / 0 | 39374 | 4658 | 48 | 8196 |
| 101 / 1 | 39303 | 4657 | 52 | 8049 |
| 102 / 0 | 39449 | 4756 | 0 | 8262 |
| 102 / 1 | 39490 | 4696 | 0 | 8193 |
| 103 / 0 | 39408 | 4028 | 149 | 7797 |
| 103 / 1 | 39670 | 3917 | 149 | 7635 |

Ground составляет89.24–90.70% прежнего interior ROI; boundary band —14.86–15.75% полного visible ROI. Узкий coded pole seed102 имеет0 interior pixels на обоих кадрах: он целиком отсутствует в прежней общей RGB-метрике, хотя отдельная coded IoU продолжала его учитывать. Нельзя интерпретировать undefined interior error как нулевую ошибку. Это ограничение определения прежней метрики, а не повреждение input или дефект renderer.

## Ошибки и конфликт критериев

Для360 conditions посчитаны pixel count, mean absolute linear RGB error, channel p95/max в каждой группе; empty masks дают null errors. Pixel-weighted сумма interiors воспроизводит прежнюю global MAE с tolerance1e−12. Историческая metric/baseline не заменены. Дополнительная full-visible MAE у medium выше старой interior MAE во120/120 conditions на0.008543…0.014834. Включение boundaries меняет смысл оценки; эти значения не сравниваются как один и тот же metric.

Ниже additional exploratory medium bowl−dome comparison на24 scene/frame/profile conditions, отрицательная разность лучше. Seed102 target interior undefined, поэтому target-interior pairs только16:

| ROI | Определённых пар bowl−dome | RGB лучше / хуже | Диапазон Δ linear MAE |
|---|---:|---:|---:|
| ground_interior | 24 | 0 / 24 | +0.009926…+0.015041 |
| ground_boundary | 24 | 0 / 24 | +0.036077…+0.052457 |
| coded_target_interior | 16 | 16 / 0 | -0.012632…-0.002416 |
| coded_target_boundary | 24 | 20 / 4 | -0.019234…+0.021591 |
| other_scene_interior | 24 | 24 / 0 | -0.086951…-0.027603 |
| other_scene_boundary | 24 | 24 / 0 | -0.026565…-0.001789 |

Bowl ухудшает ground RGB во24/24 парах, но улучшает other_scene interior/boundary во24/24. Следовательно, прежняя общая MAE отражала преимущественно дорогу и скрывала конфликт с остальными объектами. Это не universal bowl recommendation: другая geometry/scene/view/ROI может дать другой результат; other_scene не подтверждённая physical height class. Target boundary смешанный20/4, а coded-target IoU остаётся самостоятельной silhouette metric.

Стратифицированный fine−medium follow-up: ground interior RGB лучше86/120 и хуже34/120; other_scene interior лучше50/120 и хуже66/120 (четыре ties). Target boundary лучше20/120, хуже44/120 (56 ties); target interior определён в80/120, лучше3 и хуже22. Ground-heavy global улучшение не доказывает улучшения объектов или их границ. Channel p95/max сохраняются рядом со средним, но не выдаются за correspondence/ghost detector.

## Верификация и оставшиеся условия

Known-answer controls проверяют дискретные IDs/names/suffix, ego/background/visibility exclusion, union/disjointness, boundary-only thin target, unknown-ID rejection, pixel weighting, null empty groups и ошибку overlapping masks. CTest object_metrics прошёл. Полный re-audit360 conditions проверяет совпадение со SHA-pinned source metrics; исходные numeric baselines неизменны. В процессе исправлена обработка относительного capture root в новом audit path: `_verified` требует resolved containment; финальный CLI с relative paths прошёл.

Изображения ROI показывают первый кадр каждого seed, heatmap — medium/multi_band/zero, среднее двух frame errors по каждой группе; full report содержит все profiles/levels. Картинки воспроизводятся существующим `docs/diploma/plot_server_boundary.py`, новых product scripts нет. Это post-hoc stratification на одном procedural street family, не статистическая подтверждающая выборка. Границы ID не разделяют parallax/ghost/visibility/photometry, а object-ID не даёт carrier-shell coverage. Остаются physical-height truth, natural-object correspondence, отдельный carrier-hit/triangle coverage, другие views/families/long clips и реальный стенд.
