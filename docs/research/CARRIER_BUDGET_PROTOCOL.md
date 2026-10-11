# Сравнение носителей в общем ограниченном бюджете

Зафиксировано 11.10.2026 до серверного прогона новых carrier profiles. Plan: `configs/research/carrier-budget-plan.json`. Входы seeds101–103 и результаты прежнего seam solver screen уже просмотрены; этот опыт exploratory, **не независимый holdout**. Не менять геометрию, resolution, profiles или IoU threshold по новым результатам.

## Вопрос и фиксированные условия

Как меняются direct-view RGB error и coded-object IoU при разных поверхностях-носителях и близком числе треугольников? Для каждой сцены одни и те же actual perturbed cameras, RGB, pose, timestamps, direct RGB/object-ID/any-camera visibility truth. Пять носителей: plane, rectangular bowl, dome+floor, cylinder+floor, cube+floor. Plane/bowl outer half extents14m, flat half extents2.6/1.2m, bowl height1.5m; замкнутые shell radius/half extent14m, cylinder/cube height14m. Это фиксированные shapes, не оптимальные формы. Их topology, coverage и ошибка аппроксимации различаются.

Каждый носитель допускает3190–3210 active triangles (разница≤0.63%),≤1810 vertices и≤60500 bytes position/index buffers. Точное равенство всех трёх ресурсов не заявляется. План выбирает discrete resolution до результатов, учёт фактический серверный: float3 positions + unsigned indices. CPU double vertices/vector capacity, GL allocation overhead, driver copies, texture/depth/render targets, ego mesh и RSS в этот бюджет **не входят**. Output320×180, входы400×400 и four hybrid profiles одинаковы внутри всех пар. Это ограниченный mesh budget, не равная полная память/compute.

Запустить отдельный свежий server process на каждый carrier/seed, не переносить retained inactive carrier buffers между опытами. Report metadata active/resident carrier buffers должны совпасть. Серия boundary-sequence: два кадра × multi_band/graph_cut_multi_band × zero/normalized, default binary_pairs, levels4; два warmup и семь randomized measured blocks. Итого120 условий,1080 samples,840 measurement captures. Порядок carrier/seed фиксирован, randomized только порядок profiles внутри runner. No simultaneous build/CTest/Blender capture.

## Измерения и границы вывода

Read-only аудит сначала проверяет все native reports/captures прежними strict input/truth/layout/restore checks, затем mesh telemetry, размеры, frozen scenario и budgets. Primary RGB linear MAE по независимой any-camera ROI и coded-target IoU; дополнительно сохраняются false positives/missing pixels. Показывать абсолютные ошибки и парные carrier-minus-dome различия по scene/frame/profile, не pooled winner. CPU stage samples сохраняются, но последовательный порядок carriers без temperature/frequency контроля **не позволяет ранжировать скорость**. GPU compute, raster coverage и mesh density на ROI не уравнены.

Три экземпляра одного procedural street family, два кадра каждый; adjacent frames не независимые сцены. Известная true calibration исключает calibration solver из опыта. IoU магентового примитива не является natural-object ghost correspondence. При отсутствии улучшения сохранить отрицательный результат. Следующий этап — другие семейства/длинные clips, budget convergence, пространственная плотность и независимые target trials.
