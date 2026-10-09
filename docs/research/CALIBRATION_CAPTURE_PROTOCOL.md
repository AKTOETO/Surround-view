# E-CAL-capture-01: протокол distance × tilt

Зафиксирован до detector/solver результатов основной серии 10.10.2026. Это exploratory follow-up E-CAL-coverage-01, не независимое подтверждение гипотезы, сформированной до всех опытов. Геометрические диапазоны выбраны после проверки clipping; количественные исходы основного опыта ещё не просмотрены при фиксации этого протокола.

## План

- Четыре прежние radial optical families; seeds 7101/7102; clean PNG 640×480, box integration 2×2, без дополнительного blur/noise.
- Общие первые 4 central train poses из `poses(seed)`, 8 peripheral train poses на профиль, всего 12 train views во всех профилях.
- Factorial 2×2: distance=3.2/2.0 м (малое/крупное изображение доски); local tilt=0/±0.35 rad вокруг двух осей доски. Pattern знаков фиксирован по индексу ракурса. Roll и направления центров совпадают между всеми четырьмя профилями seed.
- Центры peripheral train: θ=0.65/0.8 rad, азимуты как в предыдущем генераторе; seed+1000. Изменение distance/tilt сохраняет направления центров **точно**, но изменяет угол каждого отдельного corner. Поэтому это не фиксация полного углового покрытия всех наблюдений.
- Общая validation: 4 central poses (индексы 12–15); 8 новых peripheral poses seed+2000, distance=2.6 м, local tilt=±0.175 rad. Validation не зависит от профиля. Ни detector failures, ни плохие fits не исключаются и не заменяются.
- Итого 48 уникальных PNG на family/seed: 4 central train + 4×8 peripheral train + 4 central validation + 8 peripheral validation; 384 PNG. Order=2/4 и два gate на каждый train профиль: 128 CLI attempts, 64 пары.
- Central gate на 4 центральных validation views используется для получения estimates и oracle metrics; full gate — на всех 12. Threshold p95≤1 px и monotonicity domain [0,1] rad сохраняются. Оба вызова CLI refit одинаковый train набор; совпадение estimates проверяется там, где экспортированы обе модели.
- Основные endpoints: outer-ray p95 и known-floor p95 на прежних common true controls. Дополнительно fixed-pose ошибка всех 12 validation досок, residual обоих gate, detector localization, actual θ histogram, projected cell-edge size и normal-to-ray tilt.
- Failure counts выводятся отдельно. Если central gate не экспортирует модель, oracle metric отсутствует, не равна нулю; парное улучшение на этом случае не подсчитывается. Full gate rejection не удаляет доступную central-gate estimate.
- Сравнения: large-small при каждом tilt; tilt-front при каждом distance; interactions рассматриваются описательно отдельно по family/seed/order. Никакой pooled ranking, p-value или доверительный интервал по тысячам коррелированных corners.

## Границы

Distance меняет также расстояние до доски и angular extent; tilt меняет foreshortening и corner occupancy. Это измеряемые геометрические факторы, не чистая image-resize ablation. Новые направления центров отличаются от E-CAL-coverage-01: прямые межсерийные числа не трактовать как парное сравнение. Четыре азимутальных сектора и clean radial optics не заменяют весь image domain, физические камеры, extrinsics и неровности дороги. Blur/noise и нерадиальная модель остаются отдельными последующими исследованиями.

Сохранить code/binary/image/dataset hashes и сырые outputs. Рисунки строить только из сохранённого summary/PNG. По результатам не изменять thresholds этой серии и не объявлять universal capture prescription.
