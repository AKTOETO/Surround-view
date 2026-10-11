# Парная RGB-проверка бокового ракурса

E-STITCH-carrier-lateral-01, 11.10.2026. Заранее фиксируем inputs plan `assets/scenarios/carrier-lateral-v1.json` и budget plan `configs/research/carrier-lateral-plan.json` до первого capture. Причина опыта: [[validation/CARRIER_COVERAGE]] показала, что historical view почти не включает shell.

Те же три recipe/mount seeds101–103 и coded targets, два синхронных кадра с frame_step6, camera cubefaces256px, fisheye400px, output320×180. Новая virtual camera: azimuth0.8rad, elevation0.35rad, distance8.5m, fov_y1.6rad, clips0.1…100m. Blender заново экспортирует camera RGB и независимые direct virtual RGB, object-ID, any-camera visibility и source IDs. Server не создаёт truth. Capture scripts/config/poses/входы закрепляются hashes. Пользовательская активная Blender Scene восстанавливается.

Пять medium carriers и ceilings взяты без изменения из [[research/CARRIER_BUDGET_PROTOCOL]]. Четыре native profiles: multi_band и graph_cut_multi_band × zero/normalized, по два кадра, warmup2/repeats7, seed20261011. Каждый carrier/scene запускается в свежем sv-server; свёртка выполняется production C++, команды идут через sv-client-lib и svctl research. Raw RGBA capture, reports и mesh resources сохраняются. Геометрические masks создаёт скомпилированный sv-carrier-probe с тем же source fingerprint и config.

## Критерии

Сохраняем прежний any-camera non-ego interior ROI и coded-target IoU, чтобы не менять определение метрик внутри опыта. Дополнительно на каждом кадре используем **одинаковые для всех пяти outputs** common-floor/common-shell ROIs: intersection трёх enclosure region-ID masks (floor ID1 либо shell ID2), пересечённый с независимой interior ROI. Отдельно учитываем ROI вне этих common parts. Plane/bowl также оцениваются на common-shell ROI: отсутствие геометрии считается ошибкой output, а не удаляется из оценки. Эти masks задаёт геометрия enclosure, не reconstructed RGB. Их нельзя называть физической высотой объектов; shell может отображать дорогу, а floor —поднятый объект.

Для каждого fixed ROI: pixels, linear RGB MAE, channel p95/max; empty ROI →null. Сравнивать методы в паре только на одном seed/frame/ROI. Нельзя ранжировать по carrier-dependent conditional masks без общего support. Исторические показатели нового и старого view имеют разные pixel rays/ROI, поэтому не являются парными измерениями одной ошибки; сравнение носителей допускается внутри каждого view.

Публикуем все120 quality conditions, включая отрицательные результаты, без выбора удачных сцен. Counts/shell coverage и RGB/IoU рассматриваем раздельно. Это exploratory same-family follow-up с истинной perturbed calibration, короткими клипами и coded targets; не тест калибровочного solver, natural-object correspondence, real-time FPS, физической платформы или универсального превосходства носителя. Порядок carriers фиксирован, thermal/frequency не контролируются: timings описательны, speed ranking запрещён.
