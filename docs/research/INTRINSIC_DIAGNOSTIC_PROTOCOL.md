# E-CAL-diagnostic-01: точные и обнаруженные углы при одном solver

Протокол следующей диагностики E-CAL-capture-01 фиксируется до результатов новых fit runs. Исходная factorial серия уже просмотрена: это mechanism-oriented exploratory ablation, не holdout и не подтверждающее исследование новых сцен.

## Зафиксированные условия

- Все 64 исходных сочетания family/seed/capture profile/order из `calibration-capture-v1`, те же 12 train views и общие 12 validation views; никаких новых ракурсов или исключений.
- Три train input: `analytic_truth` (double true forward UV), `analytic_truth_float32` (те же UV через float32→double), `detected_raster` (сохранённые production detector coordinates). Всего 192 вызова того же `sv::calibrate_intrinsics`.
- Во всех трёх вариантах validation UV **одинаковые, exact**; validation board pose по-прежнему отдельно подбирается OpenCV. Ошибки на известных лучах/плоскости и fixed true board poses считаются отдельно и без pose refitting.
- Неизвестная 180° indexing ambiguity доски сохраняет orientation исходного detector: при необходимости разворачиваются **truth coordinates**, detected coordinates не переставляются. Это позволяет отдельно проверить повторяемость production estimates на observed train inputs.
- Flags, order2/4, stopping criteria и monotonicity domain [0,1] сохраняются. Новый CLI режим `diagnose-intrinsics` не выполняет acceptance gate, не пишет deployable `intrinsics.json`, не меняет серверный конфиг. Successful диагностический fit не равен принятой калибровке. Отказ monotonicity/solver сохраняется; отсутствующие metrics не заменяются нулями.
- Сравнить detected estimates с исходными production central-gate estimates по всем fx/fy/cx/cy/k. Существенное расхождение >1e-7 означает, что replay не подтверждён: такой case не использовать для объяснения прежнего результата без расследования.
- Primary metrics: common outer-ray p95 и known-floor p95; дополнительно fixed true pose error, train residual, exact-validation pose-fitted residual, parameter errors. Сохранить signed detector UV bias/dispersion для каждого view, отдельно radial/tangential components относительно true principal point.
- Сравнения exact→float32 и exact→detected отдельно по case. Нулевой/малый exact residual не гарантирует нулевой oracle error при несовпадении optical family и solver order. Не сравнивать источники на разных подмножествах успешных fits без учёта отказов.

## Пределы интерпретации

Серия отделяет совокупную ошибку наблюдений от идеального-input поведения выбранного solver. Это не condition-number/uncertainty analysis и не доказывает математическую идентифицируемость. Floating-point control оценивает округление UV, но не моделирует photometric blur/noise, subpixel detector bias или неопределённость поз в физическом мире. Equisolid/stereographic и KB order2 имеют approximation error: exact observations не обязаны привести к точной полиномиальной модели. Две pose seeds не дают universal ranking.

Сохранить parent summary hash, image/detection/input hashes, binary/core/source hashes, reports и failures; отказать в выпуске summary при изменении входных hashes в течение запуска. Не изменять production thresholds по итогам диагностики.
