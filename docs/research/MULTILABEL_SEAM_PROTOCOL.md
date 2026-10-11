# E-STITCH-server-seam-01: протокол многометочного кандидата

Зафиксирован до просмотра новой серверной серии 11.10.2026. Exploratory screen на прежнем tracked `tests/data/object_stitch_v1`, не holdout. Цель — проверить native alpha-expansion candidate, его работоспособность и direct-view качество; не предполагать превосходства над legacy cut.

## Предварительные условия

- Alpha-expansion вычисляет одну weighted Potts energy на всех observed output pixels, включая single-camera anchors и 3/4-camera overlaps. Исторический binary_pairs остаётся default и имеет другую область/формулу energy. Сравнение проверяет два полных кандидата, не только optimizer при одной energy.
- Из источника [Boykov, Veksler, Zabih, 2001](https://www.cs.cornell.edu/rdz/Papers/BVZ-pami01-final.pdf) используется последовательность бинарных expansion moves для metric pairwise cost. Каждый exact min-cut move проверяется exhaustive enumeration. Глобальный optimum многометочной задачи не обещается; production ограничен восемью sweeps.
- Source, energy quantization, alpha order, tie policy и лимиты описываются в validation note до интерпретации качества. Не называть candidate permutation invariant при равных costs.

## Matrix и измерения

Обе позы checked-in fixture, исходный dome/virtual view/output320×180, graph_cut_seam и graph_cut_multi_band × binary_pairs/alpha_expansion. Boundary zero, levels4, smoothness0.1, edge24, angle2; 8 условий. Два warmup и семь measurement randomized complete blocks на каждом кадре, seed20261013. Сервера/тесты не запускать параллельно timed samples.

Native `svctl research` сохраняет actual RGBA через C++ consumer. Independent analyzer только читает report/captures: exact inputs/frame_set_id/settings/calibration/layout/hash, полнота blocks и одинаковый hash повторов обязательны. Direct RGB/object truth, independent visibility ROI и фиксированный coded-target classifier те же, что в [[research/SERVER_BOUNDARY_PROTOCOL]]. Primary показатели: linear RGB MAE, sRGB MAE, target IoU/precision/recall/false-positive/missing pixels; дополнительные — p95 channel error и stage timing samples.

Парные differences alpha_expansion−binary_pairs представляются отдельно по кадру/режиму. Сравнение median fusion_cpu по семи повторным samples — descriptive, не sustained FPS и не end-to-end latency. Negative результаты сохранять. Проверять последний baseline hash и настройки после cleanup; cursor/history не восстанавливаются. Одна сцена, две позы, resolution и dome не обосновывают универсальный ranking, качество natural-object ghosting или равнобюджетное сравнение carriers.

## Проверки ядра

Точные expansion moves: 128 seeded six-node graphs (3/4 labels), 512 binary move cases × 64 label subsets; запрет unavailable labels. Отдельно enumerate 4096 assignments на 128 других graphs; фиксировать global gap, monotonic accepted energies и отсутствие улучшающего alpha move после convergence. Эти known-answer graphs проверяют optimizer, не качество изображения. Проверить пустой граф, disconnected/forced labels, budgets/invalid inputs и actual fusion validity/empty pixels. Runtime/library/GUI проверки должны подтверждать apply/reject/restore нового параметра без записи server config.
