# E-CAL-SURVEY-01: чувствительность к измерению позы калибровочной доски

**Дата:** 07.10.2026. **Статус:** первичный sensitivity sweep на Blender dataset.  
**Базовые изображения и observations:** [[validation/IMAGE_CALIBRATION]].  
**Локальный полный JSON:** `artifacts/board-survey-v5/report.json`.

## Вопрос и метод

Как ошибка измерения положения доски в системе автомобиля влияет на оценку extrinsics? Из одного image-derived опыта повторно использованы 3 train-позы и отдельная четвёртая validation-поза для каждой камеры. Пиксельные детекции фиксированы. В train XYZ для каждой camera/board-pose преобразованы через отдельно возмущённую измеренную матрицу доски; validation XYZ, UV, камеры и intrinsics остаются точными. После этого выполняется `ransac_epnp_lm`.

Для каждого train вида добавляется независимое изотропное Gaussian-смещение translation по трём vehicle axes и независимые Gaussian углы XYZ (Euler), применённые слева к `T_vehicle_from_board`. Уровни стандартного отклонения: translation 0, 1, 2, 5, 10 мм; rotation 0, 0.05, 0.1, 0.25, 0.5°. Каждое из 25 сочетаний повторено с пятью фиксированными seed; все 125 четырёхкамерных fits успешны. В таблице RMSE и ошибки pose представлены медианой по 20 значениям (5 повторов × 4 камеры).

## Результаты

Медианный held-out reprojection RMSE, px:

| Translation σ, мм \ Rotation σ, ° | 0 | 0.05 | 0.1 | 0.25 | 0.5 |
|---:|---:|---:|---:|---:|---:|
| 0 | 0.142 | 0.311 | 0.598 | 2.156 | 4.528 |
| 1 | 0.157 | 0.297 | 0.556 | 2.056 | 4.414 |
| 2 | 0.201 | 0.350 | 0.609 | 1.923 | 4.457 |
| 5 | 0.380 | 0.460 | 0.748 | 2.376 | 5.525 |
| 10 | 0.710 | 0.946 | 1.236 | 2.446 | 4.935 |

![Влияние ошибок измерения позы доски на reprojection и оценку центра камеры](../diploma/figures/experiments/05_board_survey_sensitivity.png)

*Рисунок 1 — Медианные held-out RMSE и ошибка центра камеры. Каждая ячейка агрегирует пять seed и четыре камеры.*

При нулевом survey noise median RMSE равен 0.142 px и median center error — 6.21 мм. При чисто угловой σ=0.1° показатели возрастают до 0.598 px и 35.7 мм; при σ=0.5° — до 4.53 px и 96.5 мм. При чисто поступательной σ=10 мм получены 0.710 px и 49.7 мм. Эти числа указывают на существенное влияние ориентации шаблона в данной геометрии. Наблюдаемая немонотонность отдельных комбинаций не трактуется как эффект: пять seed и одна сцена недостаточны для точной кривой чувствительности.

## Воспроизводимость и границы

```sh
python3 tools/configurator.py calibrate-image-survey \
  --dataset artifacts/blender-board-v9 \
  --image-study artifacts/image-calibration-v2 \
  --output artifacts/board-survey --build build --seed 7731 --repeats 5
python3 docs/diploma/plot_board_survey.py \
  --report artifacts/board-survey/report.json \
  --output docs/diploma/figures/experiments/05_board_survey_sensitivity.png
```

Зафиксированные SHA-256: Blender capture `add5c4358ceeb5b36d9c4ad660fdc8220752337735910c8e84674de8bed3c095`; train observations `e4c7713f172d97de73ef0cb41313187ef351a617dd1a2bad44578f9059a23b6a`; validation observations `01ca51d7c905a201fea79c88554d03012b3ba36b6f10fc4e5b04f8b49fb992c2`; `sv-calibrate` `0cccd91b1fef23a75cba3d0cb5516ab68df1de920c3131e453d7ada84f89d543`; study source `c05b623751861101c342f750d2362600612dcf54e12f35c7ec1fe04f1f1d3bf3`.

Это модель ошибки геодезической привязки доски, а не измеренный уровень ошибки конкретного шаблона. Использована одна synthetic сцена и один комплект изображений; pose detector остаётся тем же, intrinsics известны, размер клетки точен, а held-out board pose считается безошибочной. Для практического требования нужны повторения на разных сценах и геометриях, измеренная неопределённость физического шаблона, коррелированные systematic errors и реальные фотографии. Исследовательская интерпретация — [[research/IMAGE_CALIBRATION]], следующий план — корневой `TODO.md`.

