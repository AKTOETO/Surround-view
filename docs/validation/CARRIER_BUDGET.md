# Носители при сопоставимом бюджете сетки

11.10.2026. Frozen protocol: [[research/CARRIER_BUDGET_PROTOCOL]], plan `configs/research/carrier-budget-plan.json`. Raw audit с полными native reports: [carrier_budget_v1.json](baselines/carrier_budget_v1.json). Входы прежнего seam follow-up seeds101–103 уже просмотрены; это exploratory follow-up, не независимый holdout. Исследуем carrier/fusion interaction, не универсальный выбор носителя.

## Фактические ресурсы

C++ `Renderer::mesh_resources()` возвращает scope=carrier_position_index_buffers, active и resident. Формулы учёта соответствуют `upload_mesh`: три float positions и unsigned indices. Для текущего Linux ABI float/unsigned по4 байта; GL positions packing не совпадает с CPU double Vec3. Resident включает неактивные carrier mesh buffers, сохранённые после смены типа, active — только используемый носитель. Это размеры запрошенного payload, **не измеренная полная GPU память**.

| Носитель | Треугольники | Вершины | Vertex bytes | Index bytes | Total bytes |
|---|---:|---:|---:|---:|---:|
| plane | 3196 | 1680 | 20160 | 38352 | 58512 |
| bowl | 3196 | 1680 | 20160 | 38352 | 58512 |
| dome_floor_v1 | 3200 | 1666 | 19992 | 38400 | 58392 |
| cylinder_floor_v1 | 3200 | 1730 | 20760 | 38400 | 59160 |
| cube_floor_v1 | 3208 | 1806 | 21672 | 38496 | 60168 |

Фактический диапазон3196–3208 triangles: размах12/3196=0.3755%. Общие потолки3210 triangles,1810 vertices,60500 buffer bytes соблюдены; vertex/index memory не равна (58392–60168 bytes). Оценка не включает CPU vectors/capacity, GPU driver overhead, textures/FBO/depth, ego car или RSS. Камеры400×400 и output320×180 общие. Нельзя называть это равными полными memory/compute budgets. Добавляемые линии flat-boundary make_mesh учтены фактически, поэтому uniform_cells не подменяют фактическое число треугольников.

Hand-counted native render controls проверяют regular plane25 vertices/32 triangles, dome194/352, cylinder210/352, cube150/192, размер каждой составляющей и retained-buffer accounting после переключений. Unix/TCP tests проверяют соответствие frame metadata числу triangles/bytes. Полная CTest регрессия51/51,70.45s; дополнительные auditor negative controls отклоняют retained buffers, ложные counts и превышенный потолок.

## Серия и аудит

Пятнадцать отдельных fresh server processes, по одному на seed/carrier, исключают влияние retained inactive carrier buffers; active=resident проверено во всех baseline/trial metadata. На каждом два кадра × четыре profiles (multi_band и graph_cut_multi_band × zero/normalized), два warmup и семь randomized measurement blocks:120 условий,1080 samples,840 measurement RGBA. Все reports завершены success/restored; cursor/history по-прежнему не восстанавливаются. Каждый effective config отличается от verified input config только surface. Direct view, true perturbed calibration, IDs/timestamps и independent any-camera ROI общие внутри пары.

Read-only audit проверяет frozen plan/scenario/capture recipes и helper hashes, actual profiles с defaults, shared native fingerprint, dimensions, RGBA hash/layout, input/calibration provenance, repeatability, restore и все ресурсные пределы. Source fingerprint: `1d5e030c8dab6b732c05b80be06ea3a5514b5e7b3accee38383f33b0f2cf4845`. catalog source_revision — HEAD7d79c46 с рабочими resource-telemetry изменениями; содержимое фиксируется fingerprint, не одним HEAD. Одновременно не выполнялись build/CTest/Blender capture. Carrier order фиксирован; timings сохранены как наблюдения, **скорость носителей не ранжируется** без counterbalanced order и thermal/frequency контроля.

## Парные quality результаты

| Носитель относительно dome | Диапазон Δ linear RGB MAE | Диапазон Δ coded IoU | RGB лучше / хуже из24 пар |
|---|---:|---:|---:|
| plane | +0.000014811…+0.000120689 | +0.000000000…+0.000000000 | 0 / 24 |
| bowl | +0.001648975…+0.009858366 | -0.000390819…+0.011921873 | 0 / 24 |
| cylinder_floor_v1 | -0.000004215…-0.000001781 | +0.000000000…+0.000000000 | 24 / 0 |
| cube_floor_v1 | -0.000050957…-0.000010537 | +0.000000000…+0.000034687 | 24 / 0 |

Каждая строка охватывает три seed × два кадра × четыре profiles; диапазоны — наблюдаемые extrema, не доверительные интервалы. Соседние кадры/режимы не являются24 независимыми сценами. Absolute errors по каждому frame/profile и все differences доступны в raw audit; рисунок4.53 показывает профили раздельно и среднее только двух кадров.

Plane отличается от dome немного и имеет тот же coded-target IoU во всех24 парах. Cylinder и cube уменьшают RGB proxy на небольшие величины; это не достаточное основание выбирать их вместо dome: близость согласуется с наличием общего плоского пола в выбранных видах, но доля ground/shell coverage ещё не измерена, оболочки имеют разную форму/coverage, density и approximation error, верхние/боковые ракурсы не исследованы. Bowl увеличивает RGB MAE во всех24 парах, хотя IoU улучшилась в20 и ухудшилась в4. Это конфликт метрик, а не опровержение всех bowl geometries: высота/flat size заранее фиксированы и не оптимизировались.

Дополнительная boundary ablation: normalized уменьшил RGB MAE во всех60 zero/normalized парах; coded IoU ухудшилась в16/60. Этот invariant-choice tradeoff на трёх экземплярах одного семейства не обосновывает default replacement. Вытягивание поднятых объектов сохраняется на actual server images: правильная калибровка и shell closure не восстанавливают scene depth.

## Воспроизведение и оставшиеся условия

Native `sv-server` получает config каждого carrier и один manifest; `svctl research` выполняет `configs/research/boundary-sequence.json`. Подробные команды: [[engineering/USAGE#Сравнение носителей в общем бюджете]]. Анализатор/figure script не управляют сервером и не вычисляют product fusion. Large inputs/captures остаются в artifacts; Git хранит recipe, plan, hashes/raw telemetry и производные рисунки.

Следующие условия: несколько budget levels/convergence, metric ROI отдельно ground/raised/shell, пространственная density и распределение triangle coverage, высокие/боковые virtual views, иные scene families/длинные clips, natural-object correspondence, равные texture/полные memory ceilings и target qualification. Текущая серия закрывает первый controlled mesh-budget screen, а не всё E-STITCH-01.
