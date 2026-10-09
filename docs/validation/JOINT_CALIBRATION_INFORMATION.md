# Совместный Jacobian и локальная неопределённость калибровки

Статус: завершён exploratory rerun E-CAL-information-01 от 10.10.2026. Замороженный план: [[../research/CALIBRATION_JOINT_INFORMATION_PROTOCOL]]. Полный машинный отчёт и хэши каждого Jacobian: `artifacts/calibration-joint-information-v7/summary.json`; результаты можно заново построить командой из [[../engineering/USAGE#Совместный-Jacobian-и-неопределённость-калибровки]].

## Выборка и воспроизводимость

Проверены 12 matched cases: equidistant KB order 2 и 4, а также `kb_nonzero` order 4; по два seed и профиля `small_front`/`large_front`. Каждый case рассчитан дважды — на exact synthetic UV и углах из OpenCV raster detector — всего 24 joint fits. Каждый fit включает 12 досок × 54 угла × две координаты (1296 residuals), 6/8 общих intrinsic parameters и 72 параметра nuisance poses. Все 24 задачи сошлись; ранг равен числу столбцов при machine и practical-relative `1e-10` tolerances.

В отчёте записаны хэши capture/diagnostic summaries, exact/detected datasets, numerical core и исходного Jacobian в little-endian float64. Бинарные хэши production/parent взяты из frozen diagnostic report; само локальное уточнение выполнено Python/SciPy 1.18.1, NumPy 2.5.3, Python 3.14.7. Входные capture и diagnostic summaries не менялись. Это повторный анализ уже просмотренных синтетических сцен, не независимый holdout.

## Ранг и обусловленность

Для сопоставимости параметры Jacobian нормированы: `dfx/300 px`, `dfy/295 px`, `dcx/640 px`, `dcy/480 px`, `dk_i/1`; pose increments заданы в rad/m. Поэтому condition number относится к этим единицам и не является инвариантным физическим свойством параметров.

| Модель | Origin | Медиана cond(J) | Диапазон cond(J) | Размер J |
|---|---|---:|---:|---:|
| order 2 | Exact | 2918.5 | 1874.3–4805.3 | 1296×78 |
| order 2 | Detected | 2934.6 | 1873.4–4804.2 | 1296×78 |
| order 4 | Exact | 5611.8 | 3804.5–7760.3 | 1296×80 |
| order 4 | Detected | 5590.5 | 3824.2–7578.3 | 1296×80 |

Все параметры имеют полный ранг в этих случаях. При этом добавление двух высоких коэффициентов polynomial повышает медиану cond(J) примерно в 1.9 раза. Exact и detected spectra близки, так как Jacobian оценивает геометрию модели около optimum, а не величину самого остатка. Спектр — локальная диагностика параметрической наблюдаемости, не гарантия глобальной идентифицируемости при иных poses, model mismatch или ошибках board geometry.

![Нормированный спектр совместного Jacobian](../diploma/figures/experiments/joint_calibration_singular_spectrum.png)

*Рисунок 1 — Медианный нормированный singular spectrum и межквартильный диапазон по case. Intrinsics и 12 nuisance board poses входят одновременно; отдельные order 2/4 панели нельзя напрямую сравнивать вне объявленного scaling.*

## Условные ошибки параметров

Для detector RMS по профилям получено 0.093–0.197 px. Detector-RMS covariance использует этот масштаб как условный iid scalar sigma. Она не утверждает, что коррелированные пиксельные ошибки detector действительно iid. Для отдельной проверки применён view-cluster sandwich: residuals группируются по 12 доскам. При 12 кластерах оценки остаются exploratory и не являются крупновыборочными гарантиями.

Таблица показывает по каждому случаю condition number на exact/detected данных, оценку `fx` по iid detector-RMS и кластерной модели, а также независимые fixed-pose validation p95 и RMSE восстановления пола на истинных held-out poses.

| Модель / order | Seed | Profile | cond(J), exact / detected | detector RMS, px | σ(fx), iid / cluster, px | fixed-pose p95, px | floor RMSE, m |
|---|---:|---|---:|---:|---:|---:|---:|
| equidistant / 2 | 7101 | large_front | 2856 / 2890 | 0.097 | 0.823 / 1.988 | 1.571 | 0.0133 |
| equidistant / 2 | 7101 | small_front | 4805 / 4804 | 0.193 | 1.888 / 2.303 | 2.353 | 0.0377 |
| equidistant / 2 | 7102 | large_front | 1874 / 1873 | 0.103 | 0.570 / 0.976 | 0.436 | 0.0126 |
| equidistant / 2 | 7102 | small_front | 2981 / 2979 | 0.197 | 1.186 / 1.068 | 0.915 | 0.0393 |
| equidistant / 4 | 7101 | large_front | 4083 / 4134 | 0.097 | 0.922 / 2.397 | 1.164 | 0.0128 |
| equidistant / 4 | 7101 | small_front | 7141 / 7057 | 0.193 | 2.003 / 1.926 | 1.622 | 0.0693 |
| equidistant / 4 | 7102 | large_front | 3805 / 3824 | 0.103 | 0.646 / 1.043 | 0.265 | 0.0078 |
| equidistant / 4 | 7102 | small_front | 7453 / 7578 | 0.197 | 1.295 / 0.979 | 0.897 | 0.0355 |
| kb_nonzero / 4 | 7101 | large_front | 4024 / 3964 | 0.093 | 0.835 / 1.081 | 1.117 | 0.0750 |
| kb_nonzero / 4 | 7101 | small_front | 7447 / 7047 | 0.174 | 1.811 / 1.060 | 1.374 | 0.0154 |
| kb_nonzero / 4 | 7102 | large_front | 3842 / 3886 | 0.105 | 0.674 / 0.578 | 1.889 | 0.0245 |
| kb_nonzero / 4 | 7102 | small_front | 7760 / 7532 | 0.163 | 1.113 / 1.303 | 1.313 | 0.0460 |

Значения σ(fx) относятся к локальной матрице конкретной совместной подгонки, не к физическому интервалу с гарантированным покрытием. Кластерная оценка выше iid в части опытов и ниже в других; выбор модели шума влияет на интерпретацию. Медианы по detected cases: для `small_front` σ(fx) iid/cluster составляет 1.54/1.69 px при order 2 и 1.55/1.18 px при order 4; для `large_front` — 0.70/1.48 px и 0.75/1.06 px соответственно. Малые residuals не устраняют расхождение этих оценок.

![Локальная неопределённость intrinsics](../diploma/figures/experiments/joint_calibration_intrinsic_uncertainty.png)

*Рисунок 2 — Detector-RMS iid и view-cluster standard errors для fx/fy/cx/cy; точки — медиана по seed/model, полосы — межквартильный диапазон. Кластеры — 12 synthetic board views, не независимые камеры или автомобильные поездки.*

## Интерпретация и ограничения

Exact-data fits восстановили `fx, fy, cx, cy` с максимальным абсолютным отклонением 6.7×10⁻⁹ в этих вычислениях. Их residual-based sigma округляется до нуля и является численным пределом идеальной синтетики, а не оценкой физической неопределённости. На detected data joint fits имели residual sigma 0.0876–0.1773 px; fixed-pose held-out corner RMSE по профилям имел медианы 0.985 px (`small_front`) и 0.834 px (`large_front`), а floor RMSE — 0.0385 и 0.0131 м. Coverage помогает в этой серии, но различия между seed/family исключают универсальное обещание.

Covariance использует локальную линеаризацию около найденного решения; `JᵀJ` условно обратима при выбранном rank tolerance. Такой расчёт не включает ошибку размера/плоскостности мишени, extrinsics автомобиля, нерадиальную оптику, selection detector, температурные эффекты и физический noise process. Residual sigma оценивает согласование конкретного fit, а detector RMS — только pixel-error scale относительно известной synthetic truth. Ни один из показателей не устанавливает порог допуска production gate.

Подход к least-squares и конечным разностям соответствует API [SciPy `least_squares`](https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.least_squares.html); интерпретация ковариации как локальной/asymptotic оценки требует модели ошибок, см. [NIST nonlinear least-squares standard reference data](https://www.itl.nist.gov/div898/strd/nls/data/LINKS/c-kirby2.shtml) и [NIST discussion of residual/error assumptions](https://www.itl.nist.gov/div898/handbook/pmd/section1/pmd142.htm). Исследование закрывает лишь этап full joint Jacobian/spectrum и условных iid/cluster оценок. Открыты физическая мишень с измеренной геометрией, 2D angular coverage, robust/noise ablations, другие production solver families, nonradial/extrinsic ошибки, независимые сцены и проверка gate false-accept/false-reject.
