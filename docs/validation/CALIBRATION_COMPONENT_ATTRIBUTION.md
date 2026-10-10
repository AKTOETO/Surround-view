# Разделение ответа camera intrinsics и поз досок

Статус: exploratory E-CAL-attribution-01, 10.10.2026. Protocol frozen before outcomes: [[../research/CALIBRATION_COMPONENT_ATTRIBUTION_PROTOCOL]]. Полный reproducible summary со всеми 144 signed refits, полями, хэшами и ошибками: `artifacts/calibration-attribution-v2/summary.json`. Команда: [[../engineering/USAGE#Совместный-ответ-intrinsics-и-поз-досок]].

## Метод

На exact baseline каждого из 12 matched cases вычислен полный joint Jacobian по 78/80 общим camera + nuisance-pose параметрам. Для каждого из шести equal-RMS fields (`shift_x`, `radial`, `tangential`, random seeds 1021–1023) построен линейный ответ $\Delta q_{lin}=J^+d$. На тех же 12 досках повторена nonlinear joint optimization при `uv_true ± 0.05D`; всего 144 refits. Деление между camera/pose описывает доли квадратов L2-ответа в замороженных параметрических units из protocol. Оно не является причинной долей ошибки в физических единицах.

## Результаты по направлению и модели

| UV поле | Order | Linear share intrinsics | Nonlinear share intrinsics, converged pairs | Полностью converged pairs | Median nonlinear-vs-linear L2 error |
|---|---:|---:|---:|---:|---:|
| `shift_x` | 2 | 1.000 | 1.000 | 4/4 | <0.01% |
| `shift_x` | 4 | 1.000 | 1.000 | 8/8 | <0.01% |
| `radial` | 2 | 0.060 | 0.060 | 4/4 | 0.18% |
| `radial` | 4 | 0.996 | 0.996 | 4/8 | 0.21% |
| `tangential` | 2 | 0.019 | 0.018 | 4/4 | 0.59% |
| `tangential` | 4 | 0.081 | 0.086 | 4/8 | 2.49% |
| `random` | 2 | 0.011 | 0.011 | 12/12 | 0.55% |
| `random` | 4 | 0.107 | 0.132 | 11/24 | 3.45% |

`shift_x` — known-answer control: по всем 12 cases полный J относит единичный x-shift к principal point с response 1.000000 ± 3.4×10⁻⁸ px/px; pose response близок к нулю. Максимальная относительная L2-разность nonlinear и linear responses — 0.0023%.

Для radial поля ответ зависит от порядка: order 2 обычно поглощает его nuisance poses (median intrinsic share 0.060), а у order 4 модель камеры поглощает большую долю (0.996). Но среди восьми order4 radial pairs совместно сошлись только четыре, все из `large_front`; поэтому nonlinear-цифра не переносится на `small_front`. Tangential и random поля направляют основную часть линейного ответа в pose coordinates: shares для order2 равны 0.019 и 0.011, для order4 — 0.081 и 0.107. Nonlinear complete pairs в целом согласуются с линейным ответом, однако random/order4 имеет median mismatch 3.45%, максимум 7.16%; в этой подгруппе converged только 11/24.

![Вклад intrinsics и board poses по направлениям](../diploma/figures/experiments/calibration_component_attribution.png)

*Рисунок 1 — Каждый маркер — один matched case/field. Слева показан полный first-order Jacobian response по всем cases; справа — symmetric nonlinear refit только для пар, где обе стороны достигли termination. Черта показывает медиану, подпись n — включённые наблюдения. Поэтому правый график имеет selection из-за solver convergence.*

Для полностью сошедшихся пар median pose rotation response составляет 0.28°/px для radial/order2, 0.34°/px для tangential/order2 и 1.27°/px для random/order4; corresponding translation RMS — 11.82, 11.29 и 22.29 mm/px. Эти величины масштабируют искусственное поле h=0.05 px; они не являются физической вероятностью движения камеры.

![Отклонение nonlinear fit от линейного прогноза](../diploma/figures/experiments/calibration_component_linearization_error.png)

*Рисунок 2 — Норма разности full parameter responses относительно линейного `J⁺d`, h=0.05 px. Сохранены complete signed pairs; failure pairs показаны на первом рисунке через уменьшенный n и перечислены в summary.*

## Сходимость и границы вывода

Из 144 individual fits сошлись 110; 34 остановились со status 0 после 750 function evaluations (`The maximum number of function evaluations is exceeded`). Все 34 принадлежат order 4; 18 из 21 неполной signed pair относятся к `small_front`, остальные три — к `large_front`. RMS residual неудачных последних итераций 0.00413–0.03480 px. Такой малый остаток не отменяет failure status и сам по себе не подтверждает параметры. Неудачные fits не включены в nonlinear statistics; данные сохранены и не заменены нулями.

Следовательно, полная linearized attribution доступна для всех 72 case/direction combinations, а полная nonlinear verification — только для 51 signed pairs. Направление смещения влияет на то, как solver делит ответ между intrinsics и позами; направление нельзя восстановить из общего RMS или одной condition number. Но зависимость от polynomial order, scene coverage и неудачная convergence order4 требуют дополнительной solver validation до окончательного вывода. Три random fields — фиксированные контролируемые примеры, не модель распределения detector error.

Все данные synthetic; captures повторяют ранее рассмотренные scenes. Опыт не включает detector rerun, raster blur/noise, target metrology, extrinsic/nonradial perturbations, реальные камеры или acceptance gate. Обновление production thresholds и рекомендация по camera model из него не следуют. Order4 convergence follow-up выполнен с dense Levenberg–Marquardt на тех же observations; 144/144 refits сошлись: [[CALIBRATION_COMPONENT_SOLVER]]. Sparse baseline и все её failures сохранены в `artifacts/calibration-attribution-v2`. Dense solver comparison — post-hoc exploratory, не production OpenCV validation и не независимые данные.
