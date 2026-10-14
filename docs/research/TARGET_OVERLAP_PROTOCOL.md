# Завершающий ограниченный контроль: target overlap

После §4.91 выполнить один опыт, затем перейти к ConfigService, library и GUI.
Этот control не означает завершения всех исследований; filter/noise/exposure, temporal,
natural objects и Aurora остаются явно открытыми для будущего общего ExperimentRunner.

Plan `configs/research/target-overlap-plan.json` фиксируется до geometry preflight и RGB capture.
Две новые монтажные конфигурации seeds227/229, низкий box высотой0.5m с центром
(±3.4,2.7,0.25)m, footprint1.2×1.2m, rotation±0.12rad. View azimuth0.8/2.34,
elevation0.9, distance8m, fov1.4; output320×180, fisheye400, cube256.
Материалы/renderer/budgets прежнего textured baseline. Данные preflight не используют RGB.

Geometry gate: object-ID target interior erosion1px ∩ independently source-visible≥2 cameras
должен содержать минимум16pixels в каждой сцене. После capture тот же count должен совпасть
с geometry-only preflight; иначе сохранить failure и не объявлять overlap подтверждённым.
Не искать дополнительные seeds/poses по результатам RGB. При failure записать ограничение
и всё равно перейти к продуктовым шагам, не открывать новую исследовательскую ветку.

Четыре native captures: plane/local patch, одинаковые inputs/view/fusion default внутри пары.
Проверки и ROIs наследуются от [[research/TEXTURED_SOURCE_PROTOCOL]]: lattice offset4/step8,
point tolerances2/20mm, anchors0.2mm, metric tolerance1byte, фиксированные thresholds.
Common-native-domain RGB ROI теперь заранее объявлен supplementary, не новый post-hoc отбор.
В focus report публиковать target two-camera-visible interior MAE и n; сравнивать оба carriers
на одной independent ROI. Наличие геометрической source visibility не гарантирует correct
carrier sampling или reconstruction. Существующий coarse mesh32×45 не уточнять после результата.

Exit этого bounded control: provenance и gates опубликованы, overlap counts/MAE и limitations
записаны в диплом, TODO указывает следующим действием разработку ConfigService. Даже успешный
control не ранжирует все fusion algorithms и не доказывает качество на физических камерах.
