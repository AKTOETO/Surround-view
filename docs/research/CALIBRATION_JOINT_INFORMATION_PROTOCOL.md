# E-CAL-information-01: полный Jacobian и условная неопределённость

Протокол фиксируется до дополнительных joint fits. Используются уже изученные captures E-CAL-diagnostic-01, поэтому это exploratory analysis, не независимый holdout.

## Выборка и модель

- 12 matched cases из E-CAL-capture-01: equidistant/order2 и equidistant/order4 для двух seeds×двух front profiles; kb_nonzero/order4 для двух seeds×двух front profiles. Итого по 12 train/12 validation views на case.
- Для каждого случая сравниваются только `analytic_truth` и `detected_raster` train UV; validation остаётся общей exact геометрией и не участвует в Jacobian fit. Float32, radial/nonradial family mismatch и tilted profiles повторно не оцениваются.
- Board corners — centered 9×6 grid, square=.08 m. Номинальный image size 640×480. Optical model — OpenCV KB polynomial с тем же числом коэффициентов order2/4.
- Совместный параметр: общий fx/fy/cx/cy и свободные k (6 intrinsics для order2; 8 для order4), плюс отдельные 6 pose parameters на каждый view. Значит, 78 или 80 параметров и 1296 scalar UV residuals. Board pose и intrinsics оптимизируются совместно с linear loss; pose начинается из известной synthetic pose, intrinsics — из parent production estimate. Сравнить joint estimate с parent estimate.
- Для сравнимого анализа параметр q безразмерно масштабирован: dfx/300, dfy/295, dcx/640, dcy/480, dk_i/1; per-view rotation — rad, translation — m. Singular values относятся только к этой зафиксированной нормализации.
- Jacobian получен finite differences точной radial projection; row sparsity известна: каждый corner residual зависит от общих intrinsics и только pose своей доски. Сохранить число итераций, gradient/cost, status, residuals и хэш J.

## Ранг и uncertainty

Вычислить full SVD J для **общих intrinsics и всех nuisance poses одновременно**, а не conditional Jacobian только камеры. Опубликовать singular spectrum, ratio σmax/σmin, машинный и практический ранг, weakest right-singular modes и их loadings по intrinsic/pose parameters. Не интерпретировать корреляцию как причину, если её масштаб меняется при смене объявленных units.

Для условных стандартных ошибок сохранить три оценки с чёткими названиями:

1. iid residual covariance: $\hat\sigma^2(J^TJ)^{-1}$, $\hat\sigma^2=RSS/(m-p)$, предполагает независимые одинаково распределённые scalar UV errors.
2. detector-RMS scaled covariance: $\sigma_{det}^2(J^TJ)^{-1}$, где $\sigma_{det}=\sqrt{\operatorname{mean}\|uv_{det}-uv_{true}\|^2/2}$ задан известной synthetic truth. Это задаёт масштаб предполагаемого iid error, не утверждает, что detector error iid.
3. Cluster sandwich covariance по 12 board views: $A(\sum_g s_gs_g^T)A$, $A=(J^TJ)^{-1}$, $s_g=J_g^Tr_g$, с заранее заданной finite-cluster correction $G/(G-1)·(m-1)/(m-p)$. Только 12 clusters: считать exploratory и не выдавать обычные большие выборки гарантий.

Для exact-truth source сохранить Jacobian/spectrum; остаток почти нулевой даёт только численную нижнюю оценку sigma. Не выдавать её за camera uncertainty. Если J не имеет полного column rank при заранее записанном tolerance, uncertainty по соответствующим directions считать unbounded/undefined, не заменять pseudoinverse нулями. Отдельно записать модели residual/noise assumptions.

Сравнить exact/detected spectrum, parent/joint estimate, parameter correlation, detector-RMS uncertainties и cluster covariance по каждому case. Повторённые corners внутри board не считать независимыми acquisition trials. Это локальная Gauss–Newton uncertainty около выбранного минимума: она не охватывает модельный mismatch, board-size/pose measurement errors, selection effects, nonradial distortions или реальную камеру.

## Reproducibility

Проверить hashes parent diagnostic/capture summaries, exact/detected input datasets, code/binary до и после вычислений. Совпадающие numerical core hashes обязательны. Сохранить source/version/seed/config, reports, fit statuses и failure cases. Ни gate threshold, ни server config не изменять. Не переводить эти conditional intervals в автомобильный допуск без физической метрологии и требований.
