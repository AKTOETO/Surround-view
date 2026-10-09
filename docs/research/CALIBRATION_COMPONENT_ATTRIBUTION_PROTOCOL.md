# E-CAL-attribution-01: вклад UV-направления в intrinsics и board poses

Статус: protocol frozen до запуска joint-refit outcomes, 10.10.2026. Это exploratory analysis тех же уже просмотренных synthetic captures, а не новый holdout. Предыдущие directional output metrics: [[CALIBRATION_SENSITIVITY_PROTOCOL]]; полный baseline Jacobian/spectrum: [[CALIBRATION_JOINT_INFORMATION_PROTOCOL]].

## Case selection и входы

- Зафиксировать 12 matched cases из E-CAL-capture-01: equidistant/order2 и order4, `kb_nonzero`/order4; seeds 7101/7102; profiles `small_front`/`large_front`.
- Начальная модель каждого case — exact synthetic training UV, 12 views × 54 corners. Камерные параметры и 12 nuisance board poses подгоняются совместно. Baseline должен воспроизводить synthetic truth и иметь полный rank по заранее принятому практическому tolerance 1e-10.
- Исследовать поля `shift_x`, `radial`, `tangential`, `random-1021`, `random-1022`, `random-1023`. Использовать прежний field generator и тот же field при обеих знаковых амплитудах.
- Единственный primary amplitude — h=0.05 px Euclidean RMS displacement по 648 corners. Для каждого направления создать observations `uv_true ± hD`, что даёт 144 refits плюс 12 exact baselines. Другие amplitudes и другие families/profiles не добавлять после просмотра результатов.
- Входные поля/порядок UV повторяют E-CAL-sensitivity-01. Парные baseline/refit используют одну начальную геометрию; позы каждой board могут изменяться независимо. Диагностика не вызывает production acceptance gate и не меняет server config.

## Сравнение локальной модели с joint refit

Для exact baseline Jacobian $J=\partial f/\partial q$ и вектора perturbation $d=hD$ вычислить first-order least-squares response

$$\Delta q_{lin}=\arg\min_z\|Jz-d\|_2^2=J^+d.$$

Sign convention соответствует residual `projection − observed UV`: увеличение измеренного UV на `d` требует projected UV increase `d`. Сопоставить прогноз отдельно для intrinsics q (масштабы `fx/300`, `fy/295`, `cx/640`, `cy/480`, `ki/1`) и для 72 pose increments (rotations rad, translations m). Пересчитать тот же локальный response для каждой direction; не трактовать L2 component norms как объективно равнозначные физические метрики вне этих units.

Для каждого знака ±h выполнить nonlinear joint refit, starting from exact joint optimum. Сохранить status/cost/RSS, полный-joint fitted camera parameters и позы относительно baseline. Сравнить actual signed derivative

$$\dot q_{sym}=\frac{q(+h)-q(-h)}{2h}$$

с linear Jacobian prediction $J^+D$. Также записать even component $(q(+h)+q(-h)-2q_0)/(2h)$ как показатель нелинейности на данном h. Для intrinsics публиковать физические единицы на pixel input; pose rotations — deg/px, translation — mm/px. Сводить пары по directions/cases, не считать corners независимыми trials.

## Контроли и ограничения

- `shift_x` — known-answer control: principal-point response ожидается около 1 px/px; остальные degrees of freedom должны оставаться около нуля для exact matched model. Если control не проходит numeric tolerance, приостановить причинную интерпретацию остальных fields.
- Проверить hashes exact inputs, capture/diagnostic summaries, E-CAL-sensitivity summary и source files до/после. Сохранять Jacobian SHA-256 для baseline каждого case; не сохранять большие матрицы в Git.
- Linear prediction сравнивается с нелинейным fit на конечном h=0.05 px; согласие — только локальная проверка этих directions/amplitudes, не полный глобальный response или covariance.
- Random seeds дают три конкретных reproducible fields, не выборку физических ошибок. 12 scenes являются переанализом и не независимыми acquisition trials. Здесь отсутствуют raster blur/noise, detector rerun, board metrology, extrinsic/nonradial errors и physical cameras.
- Solver failure не заменяется нулём; все cases остаются в отчёте. Не использовать результаты для gate thresholds или выбора production model без новых independent/physical data.
