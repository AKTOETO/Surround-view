# E-CAL-attribution-solver-01: численная solver ablation для joint refits

Статус: exploratory post-hoc follow-up; выбор альтернативы сделан после просмотра отказов sparse fit. Он не заменяет исходный protocol/results [[CALIBRATION_COMPONENT_ATTRIBUTION_PROTOCOL]] и не является заранее подтверждающим экспериментом.

## Вопрос и сравниваемые методы

В E-CAL-attribution-01 34 из 144 `scipy.optimize.least_squares` sparse TRF/LSMR refits достигли `max_nfev=750`; все exact baselines сошлись. Один заранее записанный failed `equidistant-7101/small_front/order4/radial/+0.05px` case повторён dense Levenberg–Marquardt. Он сошёлся за 4 evaluations с cost 0.0120648032 и residual RMS 0.00431492 px; sparse TRF потребовал 996 evaluations для практически того же cost (0.0120648572). Это мотивирует исследовать solver convergence, но само по себе не предрешает остальные 71 pairs.

- Inputs остаются без изменений: те же 12 exact matched cases, те же 6 fields/seeds и ±0.05 px, 12 train views ×54 UV, исходные board geometry и unit scaling.
- На каждом case пересчитать exact joint baseline и 12 signed nonlinear fits (144 total) методом dense `least_squares(method='lm', jac='3-point', loss='linear')`, без sparsity pattern; использовать прежние initial estimate/poses и `ftol=1e-9`, `xtol=1e-9`, `gtol=1e-8`, maximum 750 evaluations.
- Сохранить все convergence statuses, costs/residuals, parameter responses, full-Jacobian linear prediction, source/input/code hashes. Не переносить sparse result и не переиспользовать только успешные пары: все 72 cases пересчитываются тем же альтернативным solver.
- Known-answer `shift_x` должен по-прежнему передаваться в $c_x$ на 1 px/px. Сопоставить парно с исходным sparse результатом; исходный summary остаётся неизменным.

## Интерпретация

Это solver implementation comparison при fixed synthetic observations, не robust-loss comparison и не новая физическая выборка. Dense LM использует полную конечную разность и меняет trust-region алгоритм; его успех не подтверждает качества самой calibration model на реальной камере. Даже при полной сходимости вывод ограничен synthetic fields, scenes и локальными amplitudes. Не менять acceptance gate, server config или TODO threshold.
