# Программа экспериментов

## Назначение и статус

Рабочая редакция от 09.10.2026; основа от 19.09.2026 по [аудиту](../archive/AUDIT.md). Это план проверки гипотезы с отдельными уже выполненными screening-результатами. Требования — [SYSTEM.md](../requirements/SYSTEM.md), математика — [MATHEMATICS.md](../architecture/MATHEMATICS.md), профили, пороги и приёмка — [ACCEPTANCE.md](../validation/ACCEPTANCE.md). Календарь ведётся только в [ROADMAP.md](../planning/ROADMAP.md).

## Исследовательские направления

Цель, вопросы и гипотезы хранятся в [[research/TOPIC]]. Основное сравнение охватывает способы отображения, первоначальную калибровку, диагностику смещения, восстановление и целевой GPU-профиль. Новый обзор слияния камер и носителей — [[research/PROJECTION_AND_STITCHING]]. Методика калибровки: [[research/CALIBRATION]].

Дополнительный E-MESH-01 проверяет прежнюю гипотезу дискретизации: одна bowl, одинаковый допуск ошибки и разные сетки. Его выигрыш не предполагается заранее.

## Ближайшие аналоги и задачи обзора

| Источник | Установленные свойства | Следствие для постановки |
|---|---|---|
| TI, 2015, с. 9–13 [[references/DEVELOPMENT#S44|S44]] | Bowl, GPU, изменяемый виртуальный ракурс, подготовка отображений | Перечень этих функций сам по себе не обосновывает новизну |
| TI Vision Apps [[references/VISION#S12|S12]] | Раздельные графы подготовки LUT и обработки входов, настраиваемое отображение | Конфигурация и предварительные вычисления также нуждаются в предметном сравнении |
| OpenCV fisheye [[references/VISION#S04|S04]] | Формализованная модель проекции | Независимый математический эталон, а не исследовательская новизна |

Это начальное сопоставление с первичными источниками, не исчерпывающий обзор. На M1 дополнить обзор публикациями по Surround View, калибровке, контролю extrinsics и проекционным поверхностям. Для каждой работы сохранить входы, поверхность, критерий разбиения, область ракурсов, метрики, ограничения и конкретное отличие предлагаемого опыта. Задачи R1–R6 определены в [[research/TOPIC#Задачи]].

## Данные и ограничения выводов

Подготовить аналитические сцены с известными маркерами; сцены с вертикальными объектами, перекрытиями и движением; желательно хотя бы одну независимую реальную калиброванную запись. Для каждого набора указать источник, условия использования, хэши, калибровку, временную шкалу и проверочные наблюдения. Серверный модуль не должен быть единственным генератором эталонных ответов.

Использовать минимум три конфигурации автомобиля/камер с одним бинарным файлом. Наборы для настройки критерия и порогов отделить от итоговых конфигураций, точек и ракурсов. ROI определить до опыта; не подгонять её под найденное покрытие. Без реальной записи выводы о качестве ограничиваются синтетикой.

Проекционная 3D-сцена не восстанавливает скрытые поверхности; вертикальные препятствия могут смещаться и двоиться. Допустимая виртуальная камера ограничена конфигурацией. Ошибку калибровки, несоответствие условной поверхности реальной сцене и численную ошибку сетки учитывать раздельно.

## Сравниваемые варианты

| Вариант | Назначение | Условия сравнения |
|---|---|---|
| Плоскость (H=0) | Земельный baseline | Те же данные, ROI и сравнимые виды; достаточная точность сетки |
| Bowl, равномерные сетки нескольких плотностей | Исторический baseline | Та же S, камеры, blending, разрешения и GPU-проход |
| Dome+floor | Профиль 0.3.0; сравнительное качество ещё не измерено | Одинаковые фикстуры, ROI, критерии seam/marker и доступные ракурсы |
| Cylinder+floor, cube/cubemap, Burger/custom mesh | Исследовательские кандидаты, не включены в config schema | Точное определение geometry/projection, одинаковая reference-точность и бюджет |
| Та же поверхность, сетка по ошибке | Дополнительная гипотеза | Отличается только сеточное представление |
| Плотная контрольная сетка и прямая проекция | Численная точность | Та же аналитическая поверхность; независимая CPU-проверка |

Методы слияния независимо сравниваются как hard masks, текущий feather, distance-to-seam weights, graph-cut seam и graph-cut + multi-band blend. Экспозиция фиксируется или меняется отдельным фактором; скрытая компенсация между вариантами запрещена.

Сравнить при одинаковом численно проверенном допуске и при одинаковом числе треугольников. Равномерный baseline должен быть разумно настроен. Изменение формы исследуется отдельным опытом, чтобы не смешивать эффект формы с плотностью сетки. Подготовка, память и внутренняя GPU-работа учитываются отдельно от выходного readback/UI.

## Эксперименты

| ID | Сценарий | Измерения и результат |
|---|---|---|
| E-MATH-01 | Центральный луч, известные повороты, симметрия, границы FOV и сингулярности | CPU/GPU UV, валидность, NaN/Inf, p99 и максимум; тесты независимых контрольных точек |
| E-SURFACE-01 | Плоскость и несколько H/A/B при достаточной точности сетки | Покрытие ROI, положение маркеров, геометрическое двоение и яркостный шов; отдельно наземные и вертикальные объекты |
| E-STITCH-01 (protocol закрыт 09.10.2026; подтверждающая серия открыта) | 30-case depth screen и checksum-verified 84-case matrix для 6 carriers × 2 views × 7 fusion modes | Реализованы pairwise binary cuts и output-based CIE76/ghost proxies; на одной Blender capture выполнен exploratory rerun. Matched exact-scene RGB/object-ID truth и temporal metrics проверены на одном трёхкадровом клипе: [[validation/PAIRED_STITCH_TEMPORAL]]. Не подтверждены ranking, object-correspondence ghost rate, temporal stability на holdout scenes/clips и GPU/target budget: [[validation/STITCH_VISIBILITY]], [[validation/E_STITCH_01_V2]], [[research/PROJECTION_AND_STITCHING]] |
| E-MESH-01 (SHOULD) | Равномерная и исследуемая дискретизация на трёх конфигурациях | p50/p95/p99/max UV- и экранной ошибки, валидность, треугольники, память, подготовка и GPU-время; кривые ошибка/стоимость |
| E-INTERACTIVE-01 | Замороженные входы, 500 команд orbit/zoom/preset; затем движущиеся входы | Reuse ресурсов, command/state/frame linkage, частота новых видов и распределение отклика |
| E-PERF-01 | Вход 640×360, 1280×720, 1920×1080 × три плотности сетки; затем отдельный sweep выхода | Ядро и полный путь до Qt Quick; p50/p95/p99, максимальные паузы, drops, повторы, очереди и стоимость выходного адаптера |
| E-ROBUST-01 | Skew 0/10/30/100 мс, потери 0/1/5%, отказ каждой камеры, перегрузка; ошибки extrinsics ±1°/±2° и 2/5 см | Качество, покрытие, обнаружение деградации, стабильность памяти/очередей и восстановление |
| E-REAL-01 (SHOULD) | Независимая реальная запись | Перенос результата и отличия экспозиции, дисторсии, времени и позы; ограничения применимости |
| E-CALIB-01 | Intrinsics/extrinsics по разным наборам шаблона, известная истина и отложенные точки | Ошибка параметров и репроекции по камерам/видам, устойчивость, вырожденные случаи |
| E-DRIFT-01 | Смещение каждой камеры; отдельно освещение, движение, слабая текстура и skew без смещения | Чувствительность, false positives, precision/recall, локализация, время обнаружения, INDETERMINATE |
| E-RECOVERY-01 | Одна проверочная сцена до смещения, после него и после повторной калибровки | Восстановление геометрии, coverage и репроекции; версия и отчёт |
| E-TARGET-01 | Тот же алгоритм на ПК и реальном устройстве с Авророй, отдельные паспорта | CPU/GPU, p50/p95/p99/max, память, latency, drops, температура; стоимость различий профиля |

Возмущения E-ROBUST-01 — контролируемые воздействия, не статистика реальных установок. Для сравнения с истинным виртуальным видом в синтетике учитывать видимость и окклюзии; условная bowl не обязана выигрывать по каждой метрике.

## Методика и критерии выводов

Прогрев 60 с, минимум три отдельных прогона по 180 с для сравнительного sweep; для принятого эталонного профиля — дополнительный устойчивый прогон 10 мин. Сохранять паспорт CPU/GPU/драйвера/ОС/сборки, частоты и температурный режим при доступности. Порядок сравниваемых вариантов чередовать или рандомизировать с сохранением порядка. Соседние кадры одного прогона не являются независимыми повторениями опыта.

GPU execution time измерять асинхронными timer queries, CPU submission отдельно. Постоянный `glFinish` в измеряемом пути не использовать. Представлять распределения, максимумы, межпрогонный разброс и причины пропусков. Сетевые профили имеют отдельные строки результатов с полосой и погрешностью часов.

Начальные пороги из NFR уточняются по раннему baseline до итогового сравнения; сохраняются версия и причина изменения. Гипотеза поддерживается только при измеренном преимуществе в заявленных условиях. Уменьшение числа треугольников не равнозначно ускорению, если доминирует фрагментная стадия. Медленный CPU-эталон проверяет корректность и не служит доказательством общего превосходства GPU над оптимизированным CPU.

Практический результат — общее ядро, конфигуратор, клиент для Авроры, данные, калибровки и воспроизводимые протоколы для ПК и устройства. Отрицательный результат основного сравнения сохраняется и объясняется.


## Связанные источники

[[references/README|Единый каталог литературы и документации]].

Парный sampling/visibility контроль E-STITCH-01: [[validation/STITCH_RESOLUTION_VISIBILITY]] — 2 resolutions × 2 ROI policies × 7 fusion modes × 3 frames. Direct RGB/object/visibility truth идентичны между условиями. Один клип, не independent replication; timings не измеряются.

[[validation/STITCH_CONVERGENCE_ROBUSTNESS]]: convergence 256→512 при одинаковом truth и 189 frame evaluations на трёх near-obstacle layouts × трёх digital EV условиях. Это exploratory варианты одной улицы, не untouched holdout и не timing trials. Phase gates: [[planning/ROADMAP#Критерии готовности исследовательского заключения]].

## Coded-object diagnostic screen — 10.10.2026

[[validation/OBJECT_STITCH]]: общий object identity на четырёх source-camera ID maps и независимом direct truth, RGB-only target metrics, 84 carrier/fusion/frame cases. Known-answer clean/double/missing/displaced/merged controls подтверждают применимость и ограничение счётчиков копий. Два warmup/семь CPU render repeats опубликованы с raw samples. Это exploratory тест кодированной мишени, не natural-object ghost detector и не подтверждающий рейтинг; этапы 1, 5, 6 остаются частично открытыми.

## E-CAL-raster-01 — 10.10.2026

[[validation/RASTER_CALIBRATION]]: 2 pose seeds × 3 blur/noise conditions × 16 PNG, production OpenCV detector/calibrator, order=2/4 ablation. Отказы включены, localization сравнивается с forward geometric truth, intrinsics проверяются на 4 held-out views после fit 12 train views. Истинная optical family equidistant; не выдавать этот опыт за physical calibration validation.

## E-CAL-optics-01 — 10.10.2026

[[validation/OPTICAL_FAMILY_CALIBRATION]] расширяет E-CAL-raster-01 четырьмя radial optical families и новыми seeds 3101/3102. 256 PNG, 32 fits, 17 exported models, все detector/monotonicity отказы сохранены. Fixed-pose и known-floor ошибки измерены без подбора позы; выявлено ограничение принятого p95 gate при angular extrapolation. Все seeds exploratory, тест не является confirmatory holdout.

## E-CAL-coverage-01 — 10.10.2026

[[validation/CALIBRATION_COVERAGE]]: equal-budget 12 central vs 4 central + 8 peripheral train views, общие validation/ray/floor точки, 4 families × 2 seeds × order2/4. 256 PNG обнаружены, 64 fit/gate attempts, central gate accepted 32/32, full 31/32. Outer p95 order4 улучшается 8/8, floor p95 5/8. Следующая ablation должна отделить board scale/tilt от angular occupancy; текущие данные exploratory.

## E-CAL-capture-01 — 10.10.2026

[[validation/CALIBRATION_CAPTURE_FACTORS]]: 4 radial families × 2 seeds × 4 distance/tilt profiles × order2/4 × два gate =128 attempts. 384/384 PNG распознаны, 64/64 модели проходят оба gate; full residual p95<0.325 px, floor p95 до 0.155 м. Center directions одинаковы между профилями, corner angular occupancy меняется. Все парные эффекты представлены отдельно; universal capture prescription не получен. Протокол: [[CALIBRATION_CAPTURE_PROTOCOL]].

## E-CAL-diagnostic-01 — 10.10.2026

[[validation/INTRINSIC_DIAGNOSTICS]]: 64 parent cases × exact/float32/detected train UV, общие exact validation UV и known-geometry controls. 192 successful diagnostic exports, replay delta≤6.3×10⁻¹³. На matched subset max floor p95 exact≈1.15×10⁻¹¹ м, float32≈1.06×10⁻⁵ м, detected≈0.1369 м. Это diagnostic replay существующих сцен, не новое confirmatory evidence; source/parent hashes и signed localization записаны.

## E-CAL-sensitivity-01 — 10.10.2026

[[validation/CALIBRATION_SENSITIVITY]]: equal-RMS shift_x/radial/tangential/3 seeded random fields; 12 exact baselines +432 perturbed fits. Все 444 diagnostic exports успешны; 216 pair responses, invalid floor count=0. Signed input h=0.2 px может дать actual floor p95≈0.165 м при exact pose-fitted residual<0.06 px. Outputs не прошли gate: gate в серии не исполняется. Protocol [[CALIBRATION_SENSITIVITY_PROTOCOL]] зафиксирован до outcomes; parent scenes уже reviewed/exploratory.

## E-CAL-information-01 — совместная наблюдаемость и uncertainty — 10.10.2026

[[validation/JOINT_CALIBRATION_INFORMATION]]: заранее заданные 12 matched cases (equidistant order 2/4 и `kb_nonzero` order4, два seeds, два front profiles) рассчитаны для exact/detected UV, итого 24 сходящихся joint fits. Полные Jacobian имеют rank 78/78 или 80/80; order4 median condition number ≈1.9× выше order2 при фиксированном scaling. Локальные iid, detector-RMS и 12-view cluster uncertainty расходятся. Переанализ существующих synthetic scenes; не independent evidence и не physical camera uncertainty. Открыты component/pose attribution, robust-fit/noise ablations и физические captures. Frozen protocol [[CALIBRATION_JOINT_INFORMATION_PROTOCOL]].

## E-CAL-attribution-01 — 10.10.2026

[[validation/CALIBRATION_COMPONENT_ATTRIBUTION]]: 12 cases × 6 equal-RMS fields, full-Jacobian `J+ d` predictions and ±0.05 px nonlinear joint refits. Known-answer `shift_x` maps to cx at 1 px/px. Intrinsic/pose response differs by direction and polynomial order. Первичный sparse TRF runner сошёлся в 110/144 fits; исходные failures сохранены.

## E-CAL-attribution-solver-01 — 10.10.2026

[[validation/CALIBRATION_COMPONENT_SOLVER]]: после отдельной фиксации ablation protocol dense SciPy LM пересчитал те же 144 signed refits; сошлись 144/144 за 3–6 evaluations, против 110/144 у sparse TRF/LSMR. На 51 общих complete pairs median difference intrinsic share 0.00010, max 0.0253. Все исходы и source hashes сохранены. Post-hoc synthetic solver-path study объясняет неполноту первого runner, но не является production solver comparison или physical evidence.
