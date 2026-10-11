# Чувствительность качества к дискретизации носителя

Зафиксировано 11.10.2026 до новых coarse/fine прогонов. Master plan: `configs/research/carrier-refinement-plan.json`. Medium результаты уже просмотрены и immutable baseline закреплён SHA256. Inputs seeds101–103 также просмотрены; это exploratory refinement, не holdout и не способ выбрать параметры по лучшему output. Physical shapes/камеры/позы/RGB/fusion/visibility не изменять.

## Вопрос

Достаточна ли текущая дискретизация для устойчивости измеренного изображения относительно дальнейшего сгущения mesh? Это проверка sensitivity, **не доказательство сходимости к истинной сцене**. Carrier — приближение scene geometry; даже бесконечно плотный плоский пол не восстанавливает глубину поднятого объекта. Fine mesh — конечный reference для pixel-difference, не ground truth; независимый direct Blender truth остаётся отдельным источником RGB error/object IoU.

## Неизменяемые уровни

Три уровня coarse/medium/fine для каждого из пяти носителей. Размеры surfaces, flat extents и heights строго прежние; изменяются только целочисленные cells. По каждой оси приблизительно ×2, triangle count около ×4. Для dome latitude coarse минимум8; plane ny45 делится с округлением вниз22. Поэтому разные carriers на coarse/fine **не имеют равного triangle count**; сравнивать здесь каждый carrier с самим собой. Medium повторно используется из [[validation/CARRIER_BUDGET]] только после проверки hashes и совместимости source fingerprint/input/config/scenario.

Три seeds × пять carriers × два кадра × четыре profiles × три levels =360 условий. Medium сохраняет прежний 120-condition отчёт:1080 samples и840 measurement RGBA. Новые coarse/fine вместе240 условий,2160 samples,1680 measurement RGBA. Native scenario тот же boundary-sequence:2 warmup/7 measured randomized blocks, default binary_pairs, levels4. Отдельный свежий server process на seed/carrier/level; active=resident, fixed level/seed/carrier order. Никаких concurrent build/CTest/Blender rendering.

## Метрики и критерии

Для каждой scene/frame/profile/carrier сохранить actual resources, independent truth RGB MAE и coded IoU всех уровней. Посчитать signed quality differences medium−coarse и fine−medium (MAE отрицательная предпочтительна; IoU положительная), а также full RGBA changed-pixel fraction, maximum RGB8 channel delta и mean absolute **linear RGB difference** с fine внутри одной any-camera ROI. Точность до floating tolerance не подменяет byte-level parity. Уменьшение различия с fine может быть немонотонным, не исправлять такой результат tuning.

Fine reference не независим, поэтому не объявлять доказанную asymptotic convergence. Порог acceptable error/IoU для автомобиля не задан: не придумывать acceptance threshold. Сообщить наблюдаемый диапазон, tradeoff и чувствительные случаи, без pooled universal winner. Два соседних кадра/четыре profiles не независимые replicates. Coded mask threshold/min-area неизменны; это не natural-object ghost truth. Timings archival, без speed ranking из-за fixed level order и отсутствия thermal/frequency control.

Strict read-only audit проверяет frozen master/derived plans/provenance, все inputs/truth/capture hashes и recipes, shape invariance, monotonic cells/resources, source fingerprint, scenario actual defaults, complete120 условий на уровень, active/resident ceilings и restore. В Git сохранить compact audited summaries/hash references вместо повторного включения всех native metadata; raw reports/RGBA остаются в artifacts и необходимы для полного re-audit. Прежний medium baseline не перезаписывать.

Следующие этапы: spatial ground/raised/shell ROI, большие/боковые virtual views, input/output-resolution sensitivity, другие scene families/длинные clips и target budgets. Ни один finite refinement screen не закрывает их автоматически.
