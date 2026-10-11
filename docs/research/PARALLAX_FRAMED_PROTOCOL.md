# Полностью помещающийся в кадр объект и два монтажа камер

E-STITCH-parallax-framed-01, 11.10.2026. Протокол зафиксирован до capture/native outputs. Продолжение [[validation/PARALLAX_HEIGHT]] и [[validation/PARALLAX_VISIBILITY]]; прежние inputs/baselines сохраняются. Новые recipes: `assets/scenarios/parallax-framed-v1.json`, `configs/research/parallax-framed-plan.json`.

## Факторы и геометрическая приёмка

Тот же flat road100×100m, ego и emission box0.4m, без зданий и parked cars. Два mount seeds101/104, одинаковые bounds ±3° yaw/pitch и ±0.1m along body, optical heights0.85/1.12m. Это две реализации ошибки монтажа, **не изменение высот optical centers или широкий baseline sweep**. Scenario seed также проходит legacy builder; удалённые street meshes не являются второй независимой scene family. Фактические extrinsics экспортируются и подаются серверу. Calibration solver не исследуется этим опытом.

Внутри каждого rig: near XY=(2.4,2.3), far=(4.8,4.6), center z0.2/0.8/1.4m; два poses при frames0/6, straight move0.4m. Материал/размер/поворот box, мир, view и параметры камер одинаковы между факторами кроме заданного изменения. Верхние положения идеализированы без опоры. Independent direct view: azimuth0.8/elevation0.6rad, distance10m, target(0,0,0)m без поля target_m, fov_y1.6rad, clip0.1…100m. Output320×180, cubefaces256, fisheye400; source IDs выключены, independent direct ID/visibility включены.

До рендера все8 box corners преобразованы на24 case/pose сочетаниях в camera coordinates. Все лежат между near/far; projected bounds имеют минимум10.179px до borders. Frozen required margin8px проверяется повторно в audit по фактическим poses, а не по видимому ID-фрагменту. При нарушении series не принимается. Contact ID-mask с краем кадра также отвергается. Это устраняет clipping полного box, но не окклюзию его поверхностей и не изменение apparent size при переносе.

## Серверная матрица и критерии

12 cases×5 прежних medium carriers×4 profiles×2 poses =480 quality conditions;60 fresh production servers, warmup2/repeats7 дают4320 samples/3360 measurement RGBA captures. Profile order прежний: multi_band/graph_cut_multi_band × zero/normalized. Общие mesh ceilings прежние; это не сравнение полной памяти или sustained FPS. Geometry probe выполняется отдельно. Производственные алгоритмы —C++, команды —sv-client-lib/svctl research; Python остаётся research driver/oracle.

Первичные критерии публикуются вместе: full-mask coded IoU, conditional visible IoU/recall/precision по [[research/PARALLAX_VISIBILITY_PROTOCOL]], counts true/false positives/missing/ignored и visible fraction. Conditional formula/filter policy не меняются после просмотра outputs. При пустом support scores=null; нулевые/неопределённые случаи остаются в отчёте. Secondary linear RGB MAE относится к independent visible interior ROI и не заменяет silhouette criterion. Публикуются все условия, без выбора лучшего rig/pose/profile. Визуализация заранее выбирает первый pose всех12 cases и dome/multi_band/zero, плюс полную matrix means обоих poses.

Audit проверяет helper/master/local-plan hashes, input/report/RGBA hashes и повторяемость, identical rig/config/view/trajectory внутри seeds, разные camera transforms и identical non-camera config/poses между seeds, evaluated target positions, mesh inventory/budgets, restore/cursors/source fingerprint. Гипотетические center-ray ground intersections сохраняются как геометрический контроль; они не предсказывают full box/shell RGB.

## Границы вывода

Новый view изменяет direct support и projected scale; нельзя объявлять разность с прежними clipped outputs чистым эффектом удаления clipping. Сравнения rig/height/range условны на данном view/материале/разрешении. Две реализации монтажа и зависимые poses не обеспечивают независимый holdout или natural correspondence. Точечная ray visibility не гарантирует sampled appearance; min8 component filtering явно сохраняется. Fixed server order/CPU-Mesa/paused trials запрещают performance ranking и вывод об Aurora. Следующие независимые задачи: baseline/optical-height sweep, новые scene families, natural-object correspondence, input/output-resolution sensitivity, длинные клипы и target hardware.

До финального capture первая постановка с optional Blender target_m была отвергнута production config parser на первом geometry probe; серверных measurement samples нет. Unsupported plan/captures сохранены отдельно в artifacts/parallax-framed-inputs-v1 и artifacts/parallax-framed-v1/failed-master-plan.json. После проверки этой несовместимости финальный recipe использует поддерживаемый view вокруг origin, новые данные —inputs-v2/native-v2. Условия исправлены до финального capture и до любых quality results. Поле target_m не добавлялось в протокол продукта ради обхода validation.
