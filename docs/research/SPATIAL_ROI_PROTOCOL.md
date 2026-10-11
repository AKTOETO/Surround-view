# Раздельная оценка по object-ID и границам

Зафиксировано11.10.2026 до раздельного пересчёта сохранённых outputs. Все исходные изображения/общие метрики уже просмотрены: это **post-hoc exploratory stratification**, не independent holdout. Plan: `configs/research/spatial-roi-plan.json`, исходный refinement baseline закреплён SHA256 и не меняется.

## Независимые от output маски

Использовать hash-verified direct Blender ray-cast object-ID и any-camera visibility. Background0 и ego_object_ids исключить. По original object names: ground — ground/road/center dash/crosswalk/parking bay с Blender suffix `.NNN`; coded_target — diagnostic target ID; все остальные non-ego objects — other_scene. Ground — семантическая группа дороги/разметки, **не измеренная высота z=0**; other_scene включает sidewalk/curb/buildings/traffic objects и не называется строго raised. Порог физической высоты без geometric position truth не вводить. Coded target имеет заданную геометрию и рассматривается отдельно.

Общий boundary band воспроизводит прежнее исключение: max-filter3×3 ID != min-filter3×3 ID, затем binary dilation2. Для каждой из трёх групп разделить interior и boundary; пересечение с независимой any-camera visibility в обоих случаях одинаково. Получаются шесть непересекающихся ROI. Их union — весь видимый non-ego/nonbackground scene; union трёх interiors должен точно совпадать с прежней ROI из load_sequence. Band includes semantic/material object boundaries, не только силуэты и не ground-truth ghost segmentation. RGB output не участвует в построении masks.

## Измерения

Повторно проверить все360 conditions/source/config/capture/report/RGBA hashes предыдущего refinement. Для каждого six-way ROI записать число pixels, mean absolute linear RGB error относительно direct truth, channel p95 и max error. Empty ROI — pixels0 и null errors, не нулевой успешный результат. Проверить weighted sum interiors == прежняя общая linear MAE. Не усреднять six group means без pixel weights. Сохранить medium→fine paired errors по группам и counts, не объявляя соседние кадры/профили независимыми replicates. Все thresholds/name rules/band definitions фиксированы до просмотра stratified errors; не менять их под результат.

## Выводы и ограничения

Показать долю group pixels и интерпретировать, могла ли ground majority скрыть высокий object/boundary error. Error на truth boundary не различает ghost copy, parallax, visibility и photometric mismatch; этого опыта недостаточно для natural-object correspondence. Silhouette IoU прежнего coded target остаётся отдельной метрикой. Surface shell coverage нельзя получить из scene object-ID: **оно остаётся открытым** до самостоятельного carrier-hit/triangle-coverage измерения. Нет новых captures, новых scene families/ракурсов, target timings или calibration solver validation.
