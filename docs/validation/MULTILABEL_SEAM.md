# Native многометочная оптимизация швов

11.10.2026. Production candidate `fusion.seam_solver=alpha_expansion`; default `binary_pairs` сохранён. Протокол: [[research/MULTILABEL_SEAM_PROTOCOL]]. Алгоритм выполняется в `sv-fusion` внутри сервера; клиентская библиотека и C++ runner только передают параметры/получают результат. Существующие семь mode names не изменены; новый solver применяется в graph_cut_seam и graph_cut_multi_band.

## Заданная energy

Ноды — все output pixels с хотя бы одной valid камерой; четырёхсвязные рёбра соединяют observed соседей, включая single-camera anchors. Недоступные camera labels запрещены. Для valid камеры c используется EDT расстояние d_pc до invalid mask (как в legacy distance helper); при полностью valid маске сохранена его историческая convention implicit zero в (-1,0).

$$D_p(c)=\operatorname{round}\left[1000\left(1-\frac{d_{pc}}{\sum_{k\in V_p}d_{pk}}\right)\right].$$

Disagreement δ_p — maximum Euclidean linear-RGB difference между valid камерами, ноль при единственной камере. Scale s — max(1e-6, interpolated percentile95 δ) по всем observed nodes, включая single-camera pixels. Рёбра имеют **label-independent** неотрицательную стоимость:

$$w_{pq}=\operatorname{round}\left[1000\lambda\left(0.05+\frac{\min(1,\delta_p/s)+\min(1,\delta_q/s)}2\right)\right],\quad E(l)=\sum_pD_p(l_p)+\sum_{(p,q)}w_{pq}[l_p\ne l_q].$$

Это weighted Potts energy. При λ=0 pairwise penalty действительно ноль; legacy binary implementation имеет minimum integer edge cost1. Unary normalization, connected graph, anchors и pairwise definition отличаются от legacy — качество сравнивается для двух полных кандидатов. Меньшая energy нового solver не означает меньшую RGB/ghost ошибку и не сравнивается с другой legacy objective.

## Expansion move и границы

Инициализация выбирает минимальный unary cost, ties — первый camera ID. Порядок alpha фиксирован 0,1,2,3. Для каждого move x_p=0 сохраняет текущий label, x_p=1 выбирает alpha. Pair table E00/E01/E10/E11 имеет E11=0 и submodular cross=E01+E10−E00≥0. Все costs удваиваются, чтобы graph reduction не округляла половины. После добавления unary corrections и вычитания их minima exact min-cut вычисляется Boost push_relabel с int64 capacities. Forbidden label cost равен 1 + сумме максимальных finite unary и всех edge weights, строго больше стоимости любого feasible labeling. После min-cut принимается только строгое уменьшение исходной integer energy.

Основание метода: [Boykov, Veksler, Zabih, PAMI 2001](https://www.cs.cornell.edu/rdz/Papers/BVZ-pami01-final.pdf). Expansion оптимизирует большие бинарные moves; многометочный результат в общем случае приближённый. Production имеет максимум восемь sweeps и может закончить без подтверждения local convergence. По равной energy метки не меняются: candidate детерминирован для заданного порядка, но **не** обещает camera-permutation invariance при ties. Legacy uniform centrality ties сохранены.

Ядро допускает ≤262144 nodes, ≤524288 edges, unary/edge cost≤1e9, неотрицательные costs и отсутствие self-edges; дополнительно conservative total-capacity preflight учитывает сумму hard-constraint capacities при начальной saturation source arcs. Проверяется bound × (4N+16) ≤ INT64_MAX. Эти ограничения предотвращают переполнение int64. Runtime дополнительно ограничен 262144 output pixels и smoothness≤100. Это bounded work/memory, не обещание realtime на целевом устройстве.

## Диагностика и проверки

Actual alpha кадр содержит `seam_optimization`: implementation, initial/final integer energy, nodes/edges, sweeps, accepted_moves, converged, max_sweeps. Это диагностика pre-blur hard labels; в graph_cut_multi_band subsequent Gaussian smoothing/pyramid blend не оптимизируют эту energy. Renderer сбрасывает диагностику на каждом кадре, legacy/GPU paths не получают устаревшие alpha stats. Не считать отсутствие convergence флагом успешной оптимизации до local minimum.

Raw GTest JSON: [seam_optimizer_v1.json](baselines/seam_optimizer_v1.json). Независимый direct enumeration проверяет каждый exact expansion, финальную energy и admissibility; production evaluator не используется для вычисления exhaustive oracle. Качество изображения устанавливает отдельный direct-view experience, а не эти pass counts.

### Known-answer результаты

512 moves на 128 seeded six-node graphs (3/4 labels) совпали с exhaustive enumeration всех 64 binary subsets. На других 128 graphs перебраны все 4096 assignments: 125 global matches, максимальный integer gap8. Худший пример имеет expansion energy60 и global52; accepted energy снизилась77→60, дальнейших улучшающих alpha moves нет. Все 128 этих малых графов подтвердили convergence в production budget8; после одного sweep 121 ещё не подтвердил convergence. Это не гарантия того же поведения на больших изображениях.

Production controls дополнительно проверяют single-camera anchors вокруг 3/4-camera overlap: hand-computed minimum5667/5750, unavailable labels не выбираются. Пустые pixels сохраняют zero weights/RGB, known constant RGB сохраняется для hard-cut output. Equal-cost graph подтверждает deterministic ID order, не uniform tie/permutation claim. Overflow control использует допустимые individual costs с недопустимой суммой capacities и отклоняется до графового solver.

## Серверный direct-view screen

Финальная серия — `artifacts/server-seam-v3/dome_floor`, исходный tracked fixture/dome/две позы, 8 условий × (2 warmup+7 measurements) = 72 samples; 56 measurement RGBA. Полный [independent audit + native report](baselines/server_seam_v1.json) проверяет hashes/layout/inputs/settings/calibration, полноту blocks, repeatability и settings restore. Все четыре alpha условия подтвердили convergence за два sweeps и четыре принятых moves; 54402 observed nodes/108126 edges. Это число observed nodes, а не число 3/4-camera pixels.

| Поза / режим | Δ linear RGB MAE | Δ target IoU | Δ median fusion CPU, ms |
|---|---:|---:|---:|
| 0 / graph_cut_seam | +0.000000322 | 0 | +339.188 |
| 0 / graph_cut_multi_band | −0.000006732 | +0.002409056 | +336.756 |
| 1 / graph_cut_seam | +0.000000016 | 0 | +334.471 |
| 1 / graph_cut_multi_band | +0.000004603 | 0 | +343.651 |

Разности alpha_expansion−binary_pairs. Качество неоднозначно: RGB улучшилась в одной паре, ухудшилась в трёх; IoU улучшилась в одной и не изменилась в трёх. Большая стоимость включает graph construction/solver на всех observed pixels; backend CPU/GLES на AMD integrated Mesa. Scene/view/λ не перебирались в поисках выгодного результата. Уменьшение surrogate energy не устраняет carrier parallax и искажения поднятого объекта. Candidate остаётся исследовательским, не realtime recommendation.

Source fingerprint финальной серии: `fb0c890977cccf97442b9c93135c829f2997cccc6c48f2f52c768c4738c603f0`; source_revision в catalog — прежний HEAD67aa784 с рабочими изменениями. V1 не использован для timing выводов: одновременно шла сборка теста; V2 предшествует total-capacity guard. Оба промежуточных каталога сохранены локально, итоговый baseline — только V3, выполненный после сборки/CTest без их параллельного запуска.

Cursor/history не возвращаются. Default solver/formulas прежних studies сохранены; старый offline oracle **не** является эталоном alpha. Следующие условия: новые procedural cases по [[research/SEAM_GENERALIZATION_PROTOCOL]], затем разные scene families/natural-object correspondence, равные carrier budgets и target cost. Изолированное устранение fixed-label nodes может снизить стоимость без изменения energy, но требует отдельной parity проверки.

## Новые сцены и отклонения монтажа

Замороженный до получения результатов протокол: [[research/SEAM_GENERALIZATION_PROTOCOL]], recipe `assets/scenarios/seam-generalization-v1.json`. Blender 5.2.2 LTS создал три новых экземпляра процедурной улицы: низкий блок (seed101), узкий столб (seed102), поднятый короб (seed103). Варьируются здания, препятствия и монтаж камер: yaw/pitch до ±3°, along-body до ±0.1m. Это три экземпляра одного семейства, не три независимых типа мира. Исходная пользовательская Blender Scene сохранена; данные воспроизводятся скриптами без Git LFS.

Каждая сцена: два кадра, cube faces256, output320×180, четыре frozen fusion profiles, два warmup и семь randomized measurement blocks. Итого24 условия,216 samples,168 measurement RGBA. Сервер получает истинную perturbed calibration: опыт **не** проверяет восстановление калибровки. Приёмка provenance проверяет frozen recipe, generator hashes, spacing timestamps, camera/calibration/pose/visibility hashes, actual runtime settings, RGBA layout/hash, повторы и restore. Raw [seam_generalization_v1.json](baselines/seam_generalization_v1.json) содержит все native reports и результаты.

| Seed / кадр / режим | Δ linear RGB MAE | Δ target IoU | Δ median fusion CPU, ms |
|---|---:|---:|---:|
| 101 / 0 / graph_cut_seam | +0.000016328 | +0.000000000 | +820.553 |
| 101 / 0 / graph_cut_multi_band | +0.000003838 | +0.000000000 | +919.414 |
| 101 / 1 / graph_cut_seam | +0.000002178 | +0.000000000 | +921.915 |
| 101 / 1 / graph_cut_multi_band | +0.000002280 | +0.000000000 | +863.170 |
| 102 / 0 / graph_cut_seam | -0.000028211 | +0.000138122 | +898.833 |
| 102 / 0 / graph_cut_multi_band | -0.000034864 | +0.000362934 | +913.047 |
| 102 / 1 / graph_cut_seam | +0.000139408 | -0.000260301 | +769.489 |
| 102 / 1 / graph_cut_multi_band | +0.000063850 | -0.000080502 | +871.020 |
| 103 / 0 / graph_cut_seam | -0.000000236 | +0.000000000 | +840.714 |
| 103 / 0 / graph_cut_multi_band | -0.000007662 | +0.000000000 | +957.757 |
| 103 / 1 / graph_cut_seam | +0.000000084 | +0.000000000 | +928.413 |
| 103 / 1 / graph_cut_multi_band | +0.000004508 | +0.000000000 | +910.046 |

Разности alpha_expansion−binary_pairs. RGB MAE улучшилась в4/12 пар и ухудшилась в8/12; coded-target IoU улучшилась в2/12, ухудшилась в2/12 и не изменилась в8/12. Все12 alpha условий подтвердили convergence за два sweeps; принято два или три moves,54350–54401 observed nodes. Снижение surrogate energy не устранило вытягивание поднятого объекта на носителе. Силуэтные ошибки видны даже при известной истинной калибровке: выбирать solver только по его energy нельзя.

Дополнительная median fusion CPU стоимость составила769.489–957.757ms на том же Linux/Mesa host. Во время этой серии не выполнялись сборка, CTest или Blender rendering. Это внутрипарные наблюдения текущего запуска; рост относительно прежнего screen не приписывается только изменению сцены: частоты, thermal и фон ОС не зафиксированы. Нет target/GPU solver qualification или realtime claim. Пары соседних кадров не являются независимыми статистическими единицами; доверительные интервалы по12 парам не строятся.

Крупные captures остаются в `artifacts/seam-generalization-v1`; Git хранит recipe, provenance/raw reports, hashes и производные иллюстрации. Их точные hashes проверяют локальную запись; повторный Blender capture может отличаться при изменении renderer/platform. Следующие исследования: независимые семейства сцен и длинные clips, natural-object correspondence, nominal/estimated calibration ablation, равные carrier budgets и целевой стенд. Default binary_pairs сохранён; frozen сценарий не подстраивался под новые результаты.
