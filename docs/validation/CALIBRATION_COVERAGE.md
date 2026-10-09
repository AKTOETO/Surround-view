# E-CAL-coverage-01: периферийные наблюдения при одинаковом числе кадров

Дата: 10.10.2026. Продолжение [[OPTICAL_FAMILY_CALIBRATION]], частичное выполнение этапа 3 [[planning/ROADMAP]]. Production OpenCV 5.0.0 получает настоящие raster PNG через `sv-calibrate`; Python cv2 отсутствует и не требуется. Проверяются два вопроса: помогает ли замена центральных обучающих кадров периферийными и выявляет ли расширенная validation ошибку проекции, скрытую residual с подбираемой позой.

## Парный протокол

Сохранены четыре радиальные optical families, SIZE=640×480, K, 54 внутренних угла и квадрат 0.08 м из [[OPTICAL_FAMILY_CALIBRATION]]. Их формулы основаны на [авторской статье Kannala–Brandt](https://users.aalto.fi/~kannalj1/calibration/Kannala_Brandt_calibration.pdf). Оба оцениваемых профиля остаются OpenCV fisheye polynomial order=2/4; это не сравнение разных solver families. Модель и функции калибровки описаны в [официальной документации OpenCV](https://docs.opencv.org/4.13.0/db/d58/group__calib3d__fisheye.html).

Новые seeds 4101/4102. Для каждого seed/family создаются 32 уникальных изображения:

| Группа | Кадры | Использование |
|---|---:|---|
| central_train | 12 | Все для central; первые 4 для wide |
| outer_train | 8 | Только wide |
| central_validation | 4 | Общая центральная validation обоих профилей |
| outer_validation | 8 | Общая дополнительная validation обоих профилей |

Таким образом, `central` и `wide` получают **по 12 train views**, а не 12 против 20. Внутри seed пары используют одну и ту же контрольную геометрию. У периферийных train/validation разные RNG seeds: base+1000/base+2000. Генератор не отбирает кадры по успешности detector или результату solver, не повторяет неудачные позы и не исключает обязательные наблюдения при fit. Любой detector failure сохраняется и препятствует fit соответствующего набора.

Периферийная доска располагается на расстоянии 3.0–3.3 м; её нормаль направлена вдоль луча к центру. Четыре направления имеют θ=0.75 rad, ещё четыре — 0.9 rad; азимуты около ±0.6 и π±0.6 rad, с jitter ±0.025 rad и roll ±0.15 rad. Истинный max θ внутренних углов central_train составляет 0.679/0.643 rad для seeds 4101/4102, у wide — около 1.021 rad. Все внутренние углы и весь внешний border периферийных досок находятся внутри кадра; минимальный border margin в объявленных случаях >10 px.

Это **exploratory pose design**: направления выбраны после геометрической проверки clipping и detector smoke. Новые seeds не превращают просмотренные результаты в confirmatory holdout. Замена меняет одновременно angular occupancy, масштаб доски и распределение её наклонов; нельзя приписать эффект исключительно θ. Периферийные доски почти фронтальны к своим лучам. Четыре азимутальных сектора не покрывают непрерывно весь двумерный image domain.

![Растры и фактическое угловое покрытие](../diploma/figures/experiments/calibration_coverage_views.png)

*Рисунок 1 — Фактические входные PNG kb_nonzero/4101, histogram истинных углов 648 train corners для двух профилей и положения центров общих validation досок. Малый размер периферийной доски является частью ограничения опыта.*

## Два gate и отдельная известная геометрия

Для каждого train/profile/order выполняются два вызова CLI:

- `central_gate`: 4 central_validation views;
- `full_gate`: те же 4 + 8 outer_validation views.

Порог остаётся p95≤1 px, проверка монотонности — на [0,1] rad. CLI подбирает board pose отдельно на каждом validation изображении. Поскольку отдельного validation-only API нет, два вызова повторно оценивают intrinsics на **одинаковых train files и flags**. В 31 паре, где оба вызова экспортировали estimate, максимальная разность всех fx/fy/cx/cy/k равна 0. В отклонённом full-gate случае estimate не экспортируется, поэтому такую численную проверку для него не заявляем.

Метрики известной геометрии вычисляются по всем 32 **central-gate exported estimates**, включая модель, позже отклонённую full gate. Это предотвращает исключение плохого результата только из-за расширенной validation. Формулы и общие контрольные точки совпадают с [[OPTICAL_FAMILY_CALIBRATION]]: fixed true poses четырёх central_validation досок; контрольные лучи θ∈[0.02,1] с outer zone [0.7,1]; истинная плоскость Y=1.2 м, X∈[−3,3], Z∈[1,8], θ<0.95. Новые ray/plane точки не участвуют в оценивании поз или intrinsics. Invalid floor points=0 у всех 32 estimates.

Некоторые реальные углы периферийных досок достигают θ≈1.02 rad: full validation включает эти углы, хотя declared monotonicity domain заканчивается на 1 rad. Поэтому gate и геометрический ray oracle имеют немного разные границы. Accuracy на θ>1 rad по ray/plane метрикам не заявляется. Рабочий domain не расширен автоматически.

Всего: **256 PNG, 256/256 detection successes, 64 calibration attempts** (32 пары gate). Central gate экспортировал 32/32 модели, full gate — 31/32. Нет detector/monotonicity failures. Единственный full rejection — kb_nonzero/4102/central/order4: p95=1.188460 px. У wide этой пары full p95=0.296594 px. На каждой выборке validation содержит 216 или 648 corners; это correlated corners, не 216/648 независимых опытов.

## Результаты на общих контрольных лучах и плоскости

Значения p95 ниже получены отдельно для каждого case, без объединения trials разных families. Метрики wide и central имеют одинаковые истинно видимые контрольные точки внутри пары. Различия числа точек между optical families обусловлены истинным image domain.

| Family | Seed | Order | Outer p95 central → wide, px | Floor p95 central → wide, m | Full gate p95 central → wide, px |
|---|---:|---:|---:|---:|---:|
| equidistant | 4101 | 2 | 1.6125 → 0.6929 | 0.0666 → 0.0353 | 0.4637 → 0.4545 |
| equidistant | 4101 | 4 | 1.6344 → 0.6875 | 0.0655 → 0.0247 | 0.7059 → 0.4513 |
| equidistant | 4102 | 2 | 2.5644 → 3.7386 | 0.1459 → 0.1949 | 0.4494 → 0.4396 |
| equidistant | 4102 | 4 | 28.5556 → 4.2513 | 0.1531 → 0.1823 | 0.8114 → 0.4482 |
| kb_nonzero | 4101 | 2 | 0.4784 → 1.0295 | 0.0168 → 0.0251 | 0.2958 → 0.2681 |
| kb_nonzero | 4101 | 4 | 20.3242 → 1.1044 | 0.0279 → 0.0169 | 0.7919 → 0.2704 |
| kb_nonzero | 4102 | 2 | 1.6687 → 2.7940 | 0.1546 → 0.0455 | 0.3762 → 0.2924 |
| kb_nonzero | 4102 | 4 | 56.1814 → 2.8978 | 0.1600 → 0.0493 | 1.1885 → 0.2966 |
| equisolid | 4101 | 2 | 1.5000 → 0.9575 | 0.0442 → 0.0114 | 0.3721 → 0.3626 |
| equisolid | 4101 | 4 | 8.0610 → 1.1733 | 0.0462 → 0.0141 | 0.5721 → 0.3615 |
| equisolid | 4102 | 2 | 3.8005 → 2.7154 | 0.1226 → 0.1442 | 0.3978 → 0.3924 |
| equisolid | 4102 | 4 | 25.5282 → 3.2819 | 0.1282 → 0.1591 | 0.7900 → 0.3919 |
| stereographic | 4101 | 2 | 0.9999 → 2.0285 | 0.0987 → 0.0628 | 0.3424 → 0.3394 |
| stereographic | 4101 | 4 | 6.6060 → 2.2396 | 0.1000 → 0.0850 | 0.5246 → 0.3288 |
| stereographic | 4102 | 2 | 1.9169 → 1.4832 | 0.0759 → 0.1192 | 0.3920 → 0.3347 |
| stereographic | 4102 | 4 | 5.1031 → 1.7971 | 0.0771 → 0.0941 | 0.4186 → 0.3359 |

![Парные ошибки проекции и реконструкции плоскости](../diploma/figures/experiments/calibration_coverage_metrics.png)

*Рисунок 2 — Central/wide при одинаковом бюджете 12 views, отдельно order=2/4. Ошибки относятся к известной геометрии без pose refitting. Все 32 central-gate estimates доступны; отклонённая full-gate модель остаётся в сравнении.*

Для order=4 outer ray p95 уменьшился в **8/8** case, floor p95 — в **5/8**. Для order=2 оба показателя улучшились в **4/8**. Это описательные counts по четырём families и двум pose seeds, не статистическая оценка вероятности успеха.

Контрпример: equidistant/4102/order4 — outer p95 снижается 28.5556→4.2513 px, но floor p95 растёт 0.1531→0.1823 м; оба full gate accepted (0.8114 и 0.4482 px). Даже после введения peripheral training и validation малая ошибка с подбираемой позой не гарантирует метрическую точность. Equidistant/4102/order2 wide имеет floor p95=0.1949 м при full p95=0.4396 px. Без заранее объявленного физического требования это контрпримеры достаточности residual, **не рассчитанные false-accept rates**.

Практический вывод для следующего опыта: измерять θ/азимутальное покрытие вместе с размером/наклоном доски, сохранять true geometry и оценивать перенос на известную плоскость отдельно от reprojection gate. Нельзя просто рекомендовать «8 peripheral views» или повысить порядок полинома. Нужны ablation с независимыми scale/tilt factors, более крупные peripheral targets, blur/noise и полное 2D coverage. Extrinsics, tangential/decentering, road height и реальные фотографии здесь не исследованы. Threshold не изменён на основании просмотренных данных.

## Воспроизведение и проверки

```sh
cmake -S . -B build
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py compare-coverage --output artifacts/calibration-coverage-repeat --seeds 4101 4102
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_calibration_coverage.py --results artifacts/calibration-coverage-repeat
ctest --test-dir build -R 'calibration_coverage|optical_models|raster_calibration|vision_tools' --output-on-failure
```

Output должен быть новым. Python dependencies: NumPy/SciPy/Pillow/Matplotlib. Изображения и промежуточные detections/estimates остаются в `artifacts/`, восстанавливаются генератором. [Frozen summary](baselines/calibration_coverage_v1.json) содержит все detector/solver outputs, estimates, geometry, coverage, code/binary/dataset/image SHA256. Проверены 256 image hashes, 32 dataset hashes, binary и все перечисленные source hashes. Для исторических хэшей нужна версия Git этого опыта; другие версии OpenCV могут дать другие detections/fits.

Добавлены 6 regression tests: deterministic/disjoint poses; rigid rotation/normal/front-facing; весь border внутри изображения; equal view budget и фактическое peripheral coverage без фильтрации; известные board coordinates; production detector на peripheral kb_nonzero PNG. Все 8 CTest suites прошли: `calibration_job`, `vision_tools`, `optical_models`, `calibration_coverage`, `raster_calibration`, `calibration`, `real_data_calibration`, `calibration_observations`. Численные итоги всей исследовательской матрицы сохраняются отдельно от быстрых regression tests.
