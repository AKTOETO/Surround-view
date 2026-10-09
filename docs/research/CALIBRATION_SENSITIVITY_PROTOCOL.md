# E-CAL-sensitivity-01: парные направленные возмущения UV

Зафиксировать до новых fit outcomes. Это exploratory follow-up уже просмотренных synthetic captures, не physical holdout. Из E-CAL-diagnostic-01 используются exact train datasets и общая exact validation; исходные raster/detector результаты не изменяются.

## Дизайн

- Только matched модели: equidistant/order2/4 и kb_nonzero/order4. Seeds 7101/7102, profiles small_front/large_front: 12 base cases, по 12 train и 12 validation views. Остальные optical families и tilted profiles не входят в эту серию; это объявленная область, не отбор успешных fits.
- Один exact baseline refit на base case. Train perturbations: uniform shift_x, unit radial, unit tangential и три random fields (seeds 1021/1022/1023). Radial/tangential directions заданы относительно true principal point. Random Gaussian UV field имеет вычтенное mean каждого view и единичный global Euclidean RMS по 648 corners. Один random field используется при всех amplitudes/signs и сопоставляемых cases.
- Для всех направлений общий Euclidean RMS input error равен h=0.025/0.05/0.2 px; пары +h/−h. Для random это RMS длины 2D вектора, не sigma каждой координаты. Validation не возмущается.
- Всего 12 exact + 12×6 directions×3 amplitudes×2 signs =444 diagnostic fits; 216 signed pairs. Flags, stopping criteria, order и monotonicity domain [0,1] rad не меняются. Диагностические координаты имеют явный origin `controlled_perturbation`, не названы detector output или analytic truth.
- Новый CLI origin не вводит acceptance gate; outputs diagnostic_only. Solver/monotonicity failures записываются без нулевой подстановки и без повтора/замены.

## Метрики

Сохранить обычные known-ray/floor/fixed-pose errors каждого signed fit. Для успешной пары вычислить central finite response на прежних common outer rays и known-floor controls:

$$G_r(h)=p95_j\frac{\|\hat u_j(+h)-\hat u_j(-h)\|}{2h},\qquad
G_f(h)=p95_j\frac{\|\hat X_j(+h)-\hat X_j(-h)\|}{2h}.$$

Единицы — px output / px input и m / px. Это directional finite response, не condition number, не ошибка одного fitted model, не covariance или confidence interval. Сохранять counts/invalid floor intersection; оценку только на valid subset не выдавать за полный результат при invalid points.

Также сохранить signed parameter derivative [fx,fy,cx,cy,k1..k4]/(2h), norm после деления на фиксированные scales [300,295,640,480,1,1,1,1], и even response относительно exact baseline. Нормирование задано явно и не делает параметры физически равноценными. Основные endpoints — output gains; coefficient norm не использовать как рейтинг качества.

Сопоставить h=0.025 и 0.05: близость gains поддерживает локальную интерпретацию в данном диапазоне; h=0.2 проверяет изменение отклика при большем смещении. Не называть две конечные разности доказанным пределом h→0. Shift_x — known-answer control: идеальная модель может поглотить shift через cx; outer projection gain должен быть ≈1. Random fields дают три конкретных реализации, не статистически обоснованный шумовой доверительный интервал.

## Ограничения

Coordinate perturbations не заменяют raster blur/noise и detector benchmark. Систематические поля сконструированы, не оценены из физической камеры. Не вычислять full Jacobian spectrum/condition number из шести направлений. Thresholds не менять. Не включать robust-fit ablation в эти результаты: это отдельная работа. Заморозить selected source/binary/parent dataset hashes, сравнить exact baseline replay с parent estimate, сохранить raw reports/failures. При изменении inputs в течение серии summary не выпускать.
