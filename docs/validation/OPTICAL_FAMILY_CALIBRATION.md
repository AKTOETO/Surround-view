# E-CAL-optics-01: семейства оптики и проверка по известной геометрии

Дата: 10.10.2026. Продолжение [[RASTER_CALIBRATION]], частичное выполнение этапа 3 [[planning/ROADMAP]]. Исполняется production C++ OpenCV detector/calibrator; изображения синтетические. Задача — отделить residual с подбираемой board pose от ошибки восстановленной проекции при известной физической позе.

## Генеративные модели

Нормированный радиус $r/f$ задан четырьмя моделями:

| Family | Нормированный радиус |
|---|---|
| equidistant | $\theta$ |
| kb_nonzero | $\theta(1+0.12\theta^2+0.025\theta^4-0.01\theta^6+0.003\theta^8)$ |
| equisolid | $2\sin(\theta/2)$ |
| stereographic | $2\tan(\theta/2)$ |

Стандартные equidistant/equisolid/stereographic проекции и нечётный полином описаны в [Kannala–Brandt, авторская версия статьи](https://users.aalto.fi/~kannalj1/calibration/Kannala_Brandt_calibration.pdf), DOI [10.1109/TPAMI.2006.153](https://doi.org/10.1109/TPAMI.2006.153). Числа ненулевых коэффициентов выбраны для данного синтетического опыта, не измерены у реальной линзы. Радиальная симметрия и skew=0 сохраняются; tangential/decentering distortion не моделируется.

`optical_models.py` реализует прямые модели и их обратные отображения: аналитически для трёх проекций, bisection для полинома после проверки монотонности. `raster_board.py` пересекает обратные оптические лучи с плоскостью доски, затем выполняет box integration 2×2, blur/noise и uint8 quantization. Модель меняет сами входные PNG, а не только точные UV или искусственный jitter. Кэш лучей неизменяемый, ограничен восемью ключами.

## Протокол

SIZE/K/доска и диапазоны поз сохранены из [[RASTER_CALIBRATION]]: 640×480, fx/fy=300/295, principal point=(319.5,239.5), 54 угла, квадрат 0.08 м. Новые seeds 3101/3102, по 12 train + 4 validation poses. Внутри seed одинаковы позы всех optical families и условия clean/blur_noise (σ blur=1.5 px, Gaussian noise σ=0.02). Порядок методов фиксирован; производительность здесь не измеряется. Выборка exploratory, не финальная подтверждающая.

Всего 4 families × 2 seeds × 2 conditions × 16 views = 256 PNG; на каждом вызывается настоящий detector. Затем 32 calibration attempts: order=2/4, тот же gate p95≤1 px и монотонность на [0,1] rad. **Оба solver profiles остаются OpenCV fisheye polynomial:** это сравнение числа параметров на разных генеративных optical families, а не сравнение реализаций Scaramuzza/Brown/KB. Гиперпараметры после просмотра не изменялись.

Истинный maximum θ углов доски:

| Seed | Train θ max, rad | Validation θ max, rad |
|---|---:|---:|
| 3101 | 0.681011 | 0.556400 |
| 3102 | 0.530835 | 0.648518 |

Таким образом, прежний gate оценивает residual центральных наблюдений, хотя модель объявляет domain до 1 rad. Проверка монотонности необходима, но точность экстраполяции ею не обеспечивается.

![Входные оптические изображения](../diploma/figures/experiments/calibration_optics_rasters.png)

*Рисунок 1 — Один физический ракурс доски seed 3101, четыре optical families. Это рассчитанные изображения, реально поданные детектору; Blender и Python cv2 не нужны.*

## Независимая геометрическая проверка

1. **Fixed-pose board error:** четыре validation board poses фиксированы истинными. Оценённая модель проецирует их 216 физических углов; никаких `solvePnP`/pose fitting при измерении нет.
2. **Ray error:** 100 значений θ∈[0.02,1] × 128 азимутов. Включаются только точки, попадающие в истинный image domain. Ошибка в пикселях рассчитывается между истинной и оценённой проекцией одного направления; плохие estimated projections не исключаются. Зоны central [0,0.4), middle [0.4,0.7), outer [0.7,1]. Это детерминированные контрольные направления, не независимые статистические trials.
3. **Known-floor metric error:** камера имеет точно известную геометрию; плоскость в camera coordinates Y=1.2 м. X∈[−3,3] с 31 отсчётом, Z∈[1,8] с 40 отсчётами. Отбираются истинно видимые точки с θ<0.95. Их true UV инвертируются оценённой моделью и пересекаются с той же плоскостью. Ошибка — евклидово расстояние в метрах. Если estimated inverse/domain/forward intersection невалидны, это учитывается отдельным invalid count, не замалчивается как нулевая ошибка. Во всех 17 экспортированных моделях invalid floor count=0.

$$e_{fixed,j}=\|\hat\pi(R_{true}X_j+t_{true})-\pi_{true}(R_{true}X_j+t_{true})\|,$$

$$\hat X=\frac{1.2}{\hat d_y}\hat d,\qquad e_{floor}=\|\hat X-X_{true}\|.$$

Сами optical equations и физическая геометрия идеальны; независимы контрольные точки/позы и отсутствие подгонки, а не каждая строка математической реализации. Гарантии физической точности реального стенда этот synthetic oracle не даёт.

## Результаты

250/256 detector calls успешны. 17/32 fits экспортированы; 12 attempts остановились из-за отсутствующей доски в обязательном наборе, 3 — из-за немонотонной модели. Отказы сохранены; метрические результаты доступны только для экспортированных моделей. Это selection condition, поэтому нельзя ранжировать методы только по successful subset.

| Family | Seed / condition | Order | CLI p95, px | Fixed-pose p95, px | Outer ray p95, px | Floor p95, m | Result |
|---|---|---:|---:|---:|---:|---:|---|
| equidistant | 3101 / clean | 2 | 0.2975 | 0.5358 | 1.0670 | 0.0659 | accepted |
| equidistant | 3101 / clean | 4 | 0.3012 | 0.5000 | 20.4683 | 0.0596 | accepted |
| equidistant | 3101 / blur_noise | 2 | 0.4507 | 3.6382 | 5.0561 | 0.2870 | accepted |
| equidistant | 3101 / blur_noise | 4 | 0.4518 | 3.6215 | 5.8797 | 0.2860 | accepted |
| equidistant | 3102 / clean | 2 | 0.2667 | 1.4370 | 4.0057 | 0.0861 | accepted |
| equidistant | 3102 / clean | 4 | 0.2666 | 1.4850 | 71.6226 | 0.1130 | accepted |
| equidistant | 3102 / blur_noise | 2 | — | — | — | — | chessboard not detected |
| equidistant | 3102 / blur_noise | 4 | — | — | — | — | chessboard not detected |
| kb_nonzero | 3101 / clean | 2 | 0.3022 | 1.3394 | 2.0178 | 0.1537 | accepted |
| kb_nonzero | 3101 / clean | 4 | 0.3015 | 1.3601 | 4.3019 | 0.1458 | accepted |
| kb_nonzero | 3101 / blur_noise | 2 | — | — | — | — | chessboard not detected |
| kb_nonzero | 3101 / blur_noise | 4 | — | — | — | — | chessboard not detected |
| kb_nonzero | 3102 / clean | 2 | 0.2499 | 2.0583 | 2.5107 | 0.2030 | accepted |
| kb_nonzero | 3102 / clean | 4 | — | — | — | — | nonmonotonic calibrated model on declared angle domain |
| kb_nonzero | 3102 / blur_noise | 2 | — | — | — | — | chessboard not detected |
| kb_nonzero | 3102 / blur_noise | 4 | — | — | — | — | chessboard not detected |
| equisolid | 3101 / clean | 2 | 0.2774 | 0.8183 | 2.2977 | 0.0215 | accepted |
| equisolid | 3101 / clean | 4 | 0.2770 | 0.8144 | 1.9452 | 0.0213 | accepted |
| equisolid | 3101 / blur_noise | 2 | 0.4672 | 3.0273 | 4.5274 | 0.1973 | accepted |
| equisolid | 3101 / blur_noise | 4 | — | — | — | — | nonmonotonic calibrated model on declared angle domain |
| equisolid | 3102 / clean | 2 | 0.2680 | 0.7097 | 0.8948 | 0.0652 | accepted |
| equisolid | 3102 / clean | 4 | — | — | — | — | nonmonotonic calibrated model on declared angle domain |
| equisolid | 3102 / blur_noise | 2 | — | — | — | — | chessboard not detected |
| equisolid | 3102 / blur_noise | 4 | — | — | — | — | chessboard not detected |
| stereographic | 3101 / clean | 2 | 0.2793 | 1.7492 | 1.9754 | 0.1229 | accepted |
| stereographic | 3101 / clean | 4 | 0.2791 | 1.7582 | 5.5863 | 0.1237 | accepted |
| stereographic | 3101 / blur_noise | 2 | — | — | — | — | chessboard not detected |
| stereographic | 3101 / blur_noise | 4 | — | — | — | — | chessboard not detected |
| stereographic | 3102 / clean | 2 | 0.2723 | 2.4175 | 5.0182 | 0.2061 | accepted |
| stereographic | 3102 / clean | 4 | 0.2693 | 2.3742 | 61.6103 | 0.2081 | accepted |
| stereographic | 3102 / blur_noise | 2 | — | — | — | — | chessboard not detected |
| stereographic | 3102 / blur_noise | 4 | — | — | — | — | chessboard not detected |

![Репроекция и независимая метрическая ошибка](../diploma/figures/experiments/calibration_optics_metric.png)

*Рисунок 2 — Только экспортированные модели: residual с подобранной board pose по X, независимая ray/plane ошибка по Y. Левая шкала Y логарифмическая. Отказавшие модели представлены в таблице и raw report, а не точками с нулевой ошибкой.*

Пример: equidistant seed 3102 clean order=4 принят с CLI p95=0.2666 px, но outer ray p95=71.6226 px. Order=2 на тех же данных имеет outer p95=4.0057 px. Монотонный полином может резко отклоняться вне области наблюдений. Это не доказательство универсального превосходства order=2: на equisolid seed 3101 clean order=4 outer p95 ниже order=2, а результаты зависят от optical family и покрытия.

Максимальный наблюдаемый floor p95 — около 0.287 м у equidistant seed 3101 blur_noise order=2, несмотря на accepted CLI p95 около 0.451 px. Это синтетический контрпример достаточности одного reprojection gate. Расположение камеры/плоскость известны точно; влияние реальных ошибок высоты, ориентации и неровностей дороги не оценивалось.

## Воспроизведение и границы заключения

```sh
cmake -S . -B build
cmake --build build --target sv-calibrate -j 4
python3 tools/configurator.py compare-optics --output artifacts/calibration-optics-repeat --seeds 3101 3102
MPLCONFIGDIR=/tmp/sv-mpl python3 docs/diploma/plot_optics_calibration.py --results artifacts/calibration-optics-repeat
ctest --test-dir build -R 'optical_models|raster_calibration|vision_tools' --output-on-failure
```

[Сохранённый полный отчёт](baselines/calibration_optics_v1.json) содержит код/binary/dataset/image hashes, позы, detector/solver stdout/stderr, все estimates и metrics. Raw images/detections сохраняются в `artifacts/`; мир доски восстанавливается кодом. Изменённый default equidistant renderer побитно воспроизвёл прежний board-00 capture seed 2401: расширение не меняет старый профиль. Для побитного восстановления исторических code hashes нужна соответствующая версия Git; old reports не переписываются под новые исходники.

7 новых optical unit tests проверяют аналитические radii, inverse roundtrip/domain, optical axis, нулевую ошибку точной модели, focal-bias counterexample и изменение настоящих raster pixels. Пройдены 7 CTest suites: `calibration_job`, `vision_tools`, `optical_models`, `raster_calibration`, `real_data_calibration`, `calibration_observations`, `calibration`. Новый опыт также доступен через общий `tools/configurator.py compare-optics`; CLI help проверен. Source и 256 PNG hashes сверены с отчётом.

Следующие работы: расширить angular coverage training/validation до рабочего domain; измерить sensitivity к искажению/децентровке/ошибкам extrinsics; задать физические требования и false accept/reject protocol до выбора threshold. Порог 1 px автоматически не изменён: новая серия диагностирует его ограничение, но не выводит универсальный безопасный порог. Нужны физические снимки и измеренная геометрия. Все seeds этой серии уже просмотрены и не являются нетронутым holdout.
