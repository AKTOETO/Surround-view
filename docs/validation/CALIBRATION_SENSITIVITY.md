# E-CAL-sensitivity-01: одинаковый RMS, разный отклик калибровки

Дата: 10.10.2026. Продолжение [[INTRINSIC_DIAGNOSTICS]], частичное выполнение этапа 3 [[planning/ROADMAP]]. Протокол [[research/CALIBRATION_SENSITIVITY_PROTOCOL]] зафиксирован коммитом `371352c` до новых fit outcomes. Используются уже просмотренные синтетические геометрии; это exploratory sensitivity study, не physical/confirmatory holdout.

## Область и общий контроль

Только matched optical models: equidistant/order2/4 и kb_nonzero/order4; два pose seeds 7101/7102; profiles small_front/large_front. Всего **12 base cases**, по 12 train и 12 общих exact validation views. Approximation families и tilted profiles заранее исключены из области этого опыта, не удалены по результатам fit.

На каждом case повторён exact baseline. Все 12 estimates точно совпали с parent exact-input estimates, maximum parameter delta=0. Production C++ `sv::calibrate_intrinsics`, flags, stopping criteria, order и monotonicity domain [0,1] сохранены. В `diagnose-intrinsics` добавлен явный origin `controlled_perturbation`: искусственно изменённые координаты не помечаются как detector output или analytic truth. Acceptance gate не исполняется, config не изменяется.

Материалы и модель OpenCV: [[INTRINSIC_DIAGNOSTICS]], [официальный fisheye API](https://docs.opencv.org/4.13.0/db/d58/group__calib3d__fisheye.html). Числа получены собственным запуском OpenCV 5.0.0; mathematical finite differences ниже не являются source claim о гарантированной обусловленности OpenCV.

## Equal-RMS perturbations

Поля $D_{vi}\in\mathbb R^2$ имеют единичный global Euclidean RMS по 12×54=648 train corners:

$$\sqrt{\frac1{648}\sum_{v,i}\|D_{vi}\|^2}=1,\qquad u_{vi}^{\pm}=u_{vi}^{true}\pm hD_{vi}.$$

| Direction | Поле |
|---|---|
| shift_x | Один и тот же вектор (1,0) у всех corners |
| radial | Единичный вектор от true principal point к corner |
| tangential | Radial vector, повёрнутый на +90° |
| random 1021/1022/1023 | Gaussian UV fields, mean каждого view вычтен; затем один global RMS normalization |

Random directions одинаковы между amplitudes/signs и сопоставляемыми cases, поскольку shape/seed фиксированы. h=0.025/0.05/0.2 px — RMS **длины 2D ошибки**, не σ каждой координаты. Это 3 конкретных random fields, не Monte Carlo оценка вероятности ошибки реального detector. Validation UV не меняются. Optical-axis radial/tangential direction неопределено: генератор отвергает такой input вместо подстановки произвольного вектора; в выбранных train observations таких углов нет.

![Равномощные поля входных возмущений](../diploma/figures/experiments/calibration_sensitivity_fields.png)

*Рисунок 1 — Направления сконструированных UV perturbations для kb_nonzero/7101/small_front/view04. Векторы единичного глобального RMS увеличены до условного масштаба ×8 для наглядности; фактические смещения при fit равны ±hD, h≤0.2 px. Это не измеренные detector errors.*

Всего **444 fits**: 12 exact baselines + 432 perturbed fits, объединённые в 216 ±h pairs. Все завершились successful diagnostic export; monotonicity/solver failures=0. Во всех обычных floor metrics и paired response invalid floor points=0. Отказавшие fits сохранялись бы без нулевой замены и повторного подбора условий.

## Directional response, а не condition number

На одних true outer rays и true UV известной плоскости из [[OPTICAL_FAMILY_CALIBRATION]] вычисляются:

$$G_r(h)=p95_j\frac{\|\hat u_j(+h)-\hat u_j(-h)\|_2}{2h},\qquad
G_f(h)=p95_j\frac{\|\hat X_j(+h)-\hat X_j(-h)\|_2}{2h}.$$

Единицы $G_r$ — output px/input px; $G_f$ — м/input px. Это finite directional response, **не** ошибка одного fitted model относительно truth, не full Jacobian spectrum, не covariance, condition number или confidence interval. Шесть направлений не образуют полный basis пространства 1296 UV coordinates. Для floor используется общий valid intersection positive/negative/baseline с явным invalid count; значения на неполном subset нельзя выдавать за весь domain.

В отчёте также сохранены signed coefficient response [fx,fy,cx,cy,k1..k4]/(2h), norm после деления на scales [300,295,640,480,1,1,1,1], и even response $\|(f(+h)+f(-h))/2-f(0)\|/h^2$. Эти scales объявлены, но не делают параметры физически равноценными. Coefficient norm не используется как рейтинг calibration quality.

Контроль shift_x: идеальная matched model поглощает сдвиг изменением cx. Во всех cases/amplitudes $G_r\approx1$ с max численной ошибкой <2×10⁻¹¹; $G_f\approx0.02637$–0.02687 м/px на заданной плоскости. Ненулевой floor response закономерен: true UV плоскости не сдвигаются вместе с train observations.

## Наблюдаемые диапазоны отклика

Min/median/max рассчитаны по отдельным directional case p95, **не** по объединённым rays и не как статистические доверительные интервалы. Для systematic direction в строке 12 case pairs; random — 36 (12 cases×3 fields). Эти counts не означают столько независимых физических rigs.

| Direction | h, px | Pairs | Outer gain min / median / max, px/px | Floor gain min / median / max, m/px |
|---|---:|---:|---|---|
| shift_x | 0.025 | 12 | 1.00000 / 1.00000 / 1.00000 | 0.02637 / 0.02687 / 0.02687 |
| shift_x | 0.05 | 12 | 1.00000 / 1.00000 / 1.00000 | 0.02637 / 0.02687 / 0.02687 |
| shift_x | 0.2 | 12 | 1.00000 / 1.00000 / 1.00000 | 0.02637 / 0.02687 / 0.02687 |
| radial | 0.025 | 12 | 0.80918 / 2.78107 / 3.82377 | 0.03834 / 0.07254 / 0.20422 |
| radial | 0.05 | 12 | 0.80924 / 2.78112 / 3.82358 | 0.03834 / 0.07255 / 0.20422 |
| radial | 0.2 | 12 | 0.81053 / 2.78211 / 3.82103 | 0.03841 / 0.07293 / 0.20423 |
| tangential | 0.025 | 12 | 1.81700 / 3.80826 / 6.62684 | 0.16584 / 0.24431 / 0.50515 |
| tangential | 0.05 | 12 | 1.81702 / 3.80832 / 6.62719 | 0.16585 / 0.24431 / 0.50516 |
| tangential | 0.2 | 12 | 1.81737 / 3.80959 / 6.63419 | 0.16597 / 0.24438 / 0.50547 |
| random | 0.025 | 36 | 1.68280 / 6.53077 / 13.72259 | 0.04810 / 0.21005 / 0.82017 |
| random | 0.05 | 36 | 1.68207 / 6.52837 / 13.71607 | 0.04812 / 0.21057 / 0.82016 |
| random | 0.2 | 36 | 1.66649 / 6.48632 / 13.60048 | 0.04851 / 0.21662 / 0.81898 |

![Directional gain по каждому case](../diploma/figures/experiments/calibration_sensitivity_gains.png)

*Рисунок 2 — Все 72 пары при h=0.05 px: 12 base cases×6 направлений. Слева outer projection response, справа known-floor response. Цвета отражают разные физические единицы; это не per-fit error и не универсальный рейтинг методов.*

При одинаковом input RMS направления дают разный отклик. В конкретной серии random field достигает outer gain≈13.72 px/px и floor gain≈0.8202 м/px; shift_x имеет ≈1 и ≈0.0269. Это существование неодинаковой чувствительности, а не доказательство, что «random noise всегда хуже систематики». Random directions могут случайно сильнее возбуждать чувствительные сочетания K/k/poses; full singular-vector analysis здесь отсутствует.

При сравнении h=0.025 и 0.05 максимальное относительное изменение p95 gain по 72 направлениям составляет **0.3163% для outer и 0.5328% для floor**. Это поддерживает локальную интерпретацию в данном проверенном диапазоне, но не доказывает предела h→0. Между h=0.05 и 0.2 max изменение — **4.8109% и 11.6460%**. Большее смещение уже нельзя считать строго линейным во всех cases.

Пример equidistant/7102/small_front/order4/random1021 при h=0.2 px:

| Sign | Actual outer error p95, px | Actual floor error p95, m | Exact-validation pose-fitted p95, px |
|---|---:|---:|---:|
| + | 2.2584 | 0.16274 | 0.05023 |
| − | 2.1208 | 0.16498 | 0.05954 |

Это **ошибки отдельных fitted models относительно truth**, а не gains. Очень малый pose-fitted validation residual соседствует с 16-сантиметровой floor p95 даже при matched optical family и идеально известной геометрии. В этой серии gate не исполнялся; эти outputs не называются accepted и не используются для вычисления false-accept rate.

## Таблица всех directional pairs при h=0.05

Полные signed models, все три amplitudes, even response и coefficient derivatives доступны в raw summary. Ниже отдельные p95 responses, не объединённая оценка по семействам.

| Family | Seed | Profile | Order | Direction / noise seed | Outer gain, px/px | Floor gain, m/px |
|---|---:|---|---:|---|---:|---:|
| equidistant | 7101 | small_front | 2 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7101 | small_front | 2 | radial | 2.957367 | 0.038341 |
| equidistant | 7101 | small_front | 2 | tangential | 2.936098 | 0.323441 |
| equidistant | 7101 | small_front | 2 | random / 1021 | 5.096741 | 0.069776 |
| equidistant | 7101 | small_front | 2 | random / 1022 | 3.985710 | 0.416446 |
| equidistant | 7101 | small_front | 2 | random / 1023 | 6.118145 | 0.490268 |
| equidistant | 7101 | small_front | 4 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7101 | small_front | 4 | radial | 3.712428 | 0.195619 |
| equidistant | 7101 | small_front | 4 | tangential | 3.125945 | 0.312738 |
| equidistant | 7101 | small_front | 4 | random / 1021 | 3.756452 | 0.186829 |
| equidistant | 7101 | small_front | 4 | random / 1022 | 4.170830 | 0.415056 |
| equidistant | 7101 | small_front | 4 | random / 1023 | 6.938587 | 0.531295 |
| equidistant | 7101 | large_front | 2 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7101 | large_front | 2 | radial | 3.567623 | 0.073029 |
| equidistant | 7101 | large_front | 2 | tangential | 1.817020 | 0.195759 |
| equidistant | 7101 | large_front | 2 | random / 1021 | 4.103186 | 0.089950 |
| equidistant | 7101 | large_front | 2 | random / 1022 | 3.380095 | 0.298917 |
| equidistant | 7101 | large_front | 2 | random / 1023 | 5.762862 | 0.520729 |
| equidistant | 7101 | large_front | 4 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7101 | large_front | 4 | radial | 2.299510 | 0.204216 |
| equidistant | 7101 | large_front | 4 | tangential | 2.095427 | 0.182219 |
| equidistant | 7101 | large_front | 4 | random / 1021 | 2.014771 | 0.215627 |
| equidistant | 7101 | large_front | 4 | random / 1022 | 3.728498 | 0.315747 |
| equidistant | 7101 | large_front | 4 | random / 1023 | 5.863091 | 0.524619 |
| equidistant | 7102 | small_front | 2 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7102 | small_front | 2 | radial | 2.604881 | 0.045081 |
| equidistant | 7102 | small_front | 2 | tangential | 5.728076 | 0.505162 |
| equidistant | 7102 | small_front | 2 | random / 1021 | 11.567383 | 0.779325 |
| equidistant | 7102 | small_front | 2 | random / 1022 | 4.407423 | 0.123933 |
| equidistant | 7102 | small_front | 2 | random / 1023 | 8.133767 | 0.125377 |
| equidistant | 7102 | small_front | 4 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7102 | small_front | 4 | radial | 2.485841 | 0.137356 |
| equidistant | 7102 | small_front | 4 | tangential | 5.897764 | 0.499485 |
| equidistant | 7102 | small_front | 4 | random / 1021 | 10.944061 | 0.820158 |
| equidistant | 7102 | small_front | 4 | random / 1022 | 2.327368 | 0.186851 |
| equidistant | 7102 | small_front | 4 | random / 1023 | 7.880837 | 0.127231 |
| equidistant | 7102 | large_front | 2 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7102 | large_front | 2 | radial | 3.100586 | 0.056227 |
| equidistant | 7102 | large_front | 2 | tangential | 3.727988 | 0.247158 |
| equidistant | 7102 | large_front | 2 | random / 1021 | 8.961713 | 0.434853 |
| equidistant | 7102 | large_front | 2 | random / 1022 | 3.760104 | 0.073474 |
| equidistant | 7102 | large_front | 2 | random / 1023 | 7.005989 | 0.217615 |
| equidistant | 7102 | large_front | 4 | shift_x | 1.000000 | 0.026867 |
| equidistant | 7102 | large_front | 4 | radial | 0.809241 | 0.133421 |
| equidistant | 7102 | large_front | 4 | tangential | 3.888654 | 0.241466 |
| equidistant | 7102 | large_front | 4 | random / 1021 | 8.441992 | 0.461268 |
| equidistant | 7102 | large_front | 4 | random / 1022 | 1.859952 | 0.145060 |
| equidistant | 7102 | large_front | 4 | random / 1023 | 7.348052 | 0.203546 |
| kb_nonzero | 7101 | small_front | 4 | shift_x | 1.000000 | 0.026370 |
| kb_nonzero | 7101 | small_front | 4 | radial | 3.823580 | 0.072075 |
| kb_nonzero | 7101 | small_front | 4 | tangential | 5.212684 | 0.194648 |
| kb_nonzero | 7101 | small_front | 4 | random / 1021 | 7.956778 | 0.177135 |
| kb_nonzero | 7101 | small_front | 4 | random / 1022 | 13.716073 | 0.233385 |
| kb_nonzero | 7101 | small_front | 4 | random / 1023 | 10.620124 | 0.139723 |
| kb_nonzero | 7101 | large_front | 4 | shift_x | 1.000000 | 0.026370 |
| kb_nonzero | 7101 | large_front | 4 | radial | 3.041421 | 0.108810 |
| kb_nonzero | 7101 | large_front | 4 | tangential | 3.313886 | 0.165850 |
| kb_nonzero | 7101 | large_front | 4 | random / 1021 | 5.493956 | 0.157243 |
| kb_nonzero | 7101 | large_front | 4 | random / 1022 | 11.959057 | 0.255527 |
| kb_nonzero | 7101 | large_front | 4 | random / 1023 | 7.299507 | 0.124707 |
| kb_nonzero | 7102 | small_front | 4 | shift_x | 1.000000 | 0.026370 |
| kb_nonzero | 7102 | small_front | 4 | radial | 2.439500 | 0.047522 |
| kb_nonzero | 7102 | small_front | 4 | tangential | 6.627185 | 0.310013 |
| kb_nonzero | 7102 | small_front | 4 | random / 1021 | 9.550528 | 0.205520 |
| kb_nonzero | 7102 | small_front | 4 | random / 1022 | 2.255933 | 0.171187 |
| kb_nonzero | 7102 | small_front | 4 | random / 1023 | 10.027712 | 0.450338 |
| kb_nonzero | 7102 | large_front | 4 | shift_x | 1.000000 | 0.026370 |
| kb_nonzero | 7102 | large_front | 4 | radial | 1.390398 | 0.058821 |
| kb_nonzero | 7102 | large_front | 4 | tangential | 5.127182 | 0.218810 |
| kb_nonzero | 7102 | large_front | 4 | random / 1021 | 7.991823 | 0.108506 |
| kb_nonzero | 7102 | large_front | 4 | random / 1022 | 1.682067 | 0.048124 |
| kb_nonzero | 7102 | large_front | 4 | random / 1023 | 8.384182 | 0.384804 |

## Воспроизведение и проверка

Нужен parent E-CAL-diagnostic-01 с exact JSON datasets и соответствующий capture summary. Для исторической серии:

```sh
cmake -S . -B build
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py compare-sensitivity --input artifacts/calibration-diagnostic-v2 --capture-summary docs/validation/baselines/calibration_capture_v1.json --output artifacts/calibration-sensitivity-repeat
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_sensitivity.py --results artifacts/calibration-sensitivity-repeat --input artifacts/calibration-diagnostic-v2
ctest --test-dir build -R 'calibration_sensitivity|intrinsic_diagnostics|vision_tools|optical_models' --output-on-failure
```

Если parent отсутствует, восстановить capture и diagnostic по [[INTRINSIC_DIAGNOSTICS]]. После нового capture передать его `summary.json` через `--capture-summary`: он обязан совпадать с hash parent diagnostics. Исторический frozen summary не заменяет произвольный новый parent. Numerical core/source hashes должны соответствовать; другие toolchain/OpenCV версии могут менять результаты. Output новый, Python NumPy/SciPy/Pillow/Matplotlib и C++ OpenCV binary; Blender/Python cv2 не нужны.

[Сохранённый summary](baselines/calibration_sensitivity_v1.json): 444 reports/estimates, 216 responses, source/binary/parent/dataset hashes, measured RMS, all process outputs. Проверены 432 perturbed input hashes, actual RMS в пределах 1e-12 px от h, неизменная validation и disjoint view IDs, 8 parent exact inputs, 12 replay baselines, selected source/binary/capture/parent hashes. Скрипт отказывается выпускать summary, если замороженные source/binary/parent inputs изменились во время серии. Raw datasets/reports остаются в `artifacts/`, воспроизводятся кодом.

7 новых regression tests проверяют equal RMS/sign symmetry; radial/tangent orientation; random seed и zero per-view mean; analytic shift_x gain/parameter units; совпадение floor domain с прежним validator и известную точку (0,1.2,8); missing metrics/invalid inputs; настоящий C++ fit controlled origin с cx shift. Все 11 выбранных CTest suites прошли: `calibration_job`, `vision_tools`, `optical_models`, `calibration_coverage`, `calibration_capture_factors`, `intrinsic_diagnostics`, `calibration_sensitivity`, `raster_calibration`, `calibration`, `real_data_calibration`, `calibration_observations`.

Следующие задачи: full normalized Jacobian/spectrum и uncertainty, targeted component/pose analysis, robust-fit ablation без отбора по oracle; paired raster blur/noise, nonradial/extrinsic/ground-height effects; физические требования и измеренные снимки. Equal-RMS coordinate perturbations не заменяют реальную фотометрию/детектор, а synthetic response не обосновывает production threshold или работоспособность на Авроре.
