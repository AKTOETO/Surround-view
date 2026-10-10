# Сходимость joint component-attribution fit

Статус: exploratory post-hoc solver follow-up, E-CAL-attribution-solver-01, 10.10.2026. Замороженные данные и solver comparison protocol: [[../research/CALIBRATION_COMPONENT_SOLVER_PROTOCOL]]. Исходная sparse серия с failures сохранена в [[CALIBRATION_COMPONENT_ATTRIBUTION]] и не переписана.

## Парная solver ablation

Первоначальный joint refit использовал разреженный конечный Jacobian и TRF/LSMR. Он сошёлся в 110/144 individual refits (51/72 complete signed pairs); 34 status=0 достигли лимита в 750 evaluations, все были order4. На одном из этих inputs dense Levenberg–Marquardt достиг практически того же cost за 4 function evaluations, в то время как sparse TRF при более длинном запуске потребовал 996. Это послужило post-hoc гипотезой о solver-path, а не о недостаточной геометрии observations.

После фиксации отдельного protocol все 12 cases, 6 полей/seed и ±0.05 px стороны были полностью пересчитаны одним альтернативным методом: SciPy `least_squares(method='lm', jac='3-point')`, linear loss, `ftol=1e-9`, `xtol=1e-9`, `gtol=1e-8`, `max_nfev=750`, dense finite differences без sparsity. Итог: **144/144 refits сошлись**, по 3–6 evaluations в выводе SciPy. Измерения по camera/pose outputs ниже относятся к полному dense-LM набору; исходные sparse failures сохраняются как baseline.

| Field | Order | Intrinsic share `J+ d` | Intrinsic share LM symmetric refit | Complete pairs | Median `||actual−linear||/||linear||` |
|---|---:|---:|---:|---:|---:|
| `shift_x` | 2 | 1.000 | 1.000 | 4/4 | <0.001% |
| `shift_x` | 4 | 1.000 | 1.000 | 8/8 | <0.001% |
| `radial` | 2 | 0.060 | 0.060 | 4/4 | 0.005% |
| `radial` | 4 | 0.996 | 0.996 | 8/8 | 0.001% |
| `tangential` | 2 | 0.019 | 0.019 | 4/4 | 0.006% |
| `tangential` | 4 | 0.081 | 0.081 | 8/8 | 0.008% |
| `random` | 2 | 0.011 | 0.011 | 12/12 | 0.067% |
| `random` | 4 | 0.107 | 0.107 | 24/24 | 0.138% |

Максимальная relative L2 error по LM серии 0.629% (random/order4); median по всем 72 directions — 0.0139%. `shift_x` даёт `dcx/dh=1.0000000000 px/px` для всех cases. Median pose rotation/translation responses: radial 0.251°/px и 6.87 mm/px; tangential 0.345°/px и 13.06 mm/px; random 1.80°/px и 22.96 mm/px. Единицы нормируют response на artificial field h=0.05 px, не являются физической частотой или ошибкой монтажа.

На 51 pairs, где sparse TRF ранее сошёлся с обеими сторонами, LM и TRF дали близкие camera-versus-pose shares: median absolute difference intrinsic share 0.00010, максимум 0.0253. Это показывает, что ранее сошедшиеся fits согласованы с LM для этой выборки; не доказывает универсальную эквивалентность solver methods.

![Полное распределение joint response при dense LM](../diploma/figures/experiments/calibration_component_attribution_lm.png)

*Рисунок 1 — Все 72 directions: linear Jacobian prediction и nonlinear LM refits. В отличие от sparse baseline здесь обе стороны каждой пары сошлись; n показывает 12 cases на systematic field и 36 random realizations.*

![LM linearization error](../diploma/figures/experiments/calibration_component_linearization_error_lm.png)

*Рисунок 2 — Relative L2 difference между symmetric LM response и `J+ d` при h=0.05 px. Все signed pairs включены; это finite local agreement в параметрических units, не model accuracy metric.*

## Вывод и ограничения

Проблема массовых отказов order4 в данном Python joint-analysis tool объясняется выбранным sparse TRF/LSMR path: dense LM сходится на тех же inputs без отбора, а на общей подвыборке его ответы близки. Поэтому полный signed nonlinear attribution теперь доступен для всех объявленных matched cases. Это не означает, что order4 модель точнее order2: response share меняется с порядком, radial field для order2 в основном идёт в board poses, а для order4 — в intrinsics.

Изменение алгоритма выбрано post-hoc после анализа первичного статуса; follow-up — exploratory. SciPy dense LM является исследовательским solver path и не заменяет production OpenCV fisheye calibrator. Все входы синтетические, а directions и h заданы искусственно. Независимые scenes, detector/raster stress, физические измерения и target-platform validation остаются открытыми. Ни production gate, ни server config не изменялись.

Воспроизводимость: `artifacts/calibration-attribution-lm-v1/summary.json` хранит full joint baselines, все refits, source/input/field/Jacobian hashes и versions. Запуск и тесты: [[../engineering/USAGE#Совместный-ответ-intrinsics-и-поз-досок]].
