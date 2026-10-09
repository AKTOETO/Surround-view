# Актуальный аудит документации и реализации

Дата сверки: 09.10.2026. Проверены рабочие инструкции, план, TODO, требования, протокол, приложения, Blender capture/converter, исследования и подтверждающие тестовые отчёты. Исторические validation reports описывают ровно ту revision, что указана в самом отчёте; их нельзя трактовать как измерения текущего HEAD.

## Состояние по главным этапам

| Направление | Подтверждено в проекте | Что остаётся открытым |
|---|---|---|
| Математика, GPU и четыре камеры | Независимые CPU-модели, OpenCV projection/calibration tools, GLES renderer, plane/bowl/dome-floor/cylinder/cube, hard/edge/angular fusion, карты покрытия и веса; доступны RTX/Mesa baselines | Сравнение качества с единым depth/visibility oracle, одинаковые бюджеты и независимые validation scenes |
| Blender стенд | Процедурный мир с автомобилем, разнесёнными центрами камер, заданными позами/траекторией; replay/socket producer; Qt Quick 3D driving preview | Preview только визуальный: он не отправляет 4 синхронных live-кадра и не создаёт calibration observations. Нет physics/полноценной модели автомобиля |
| Depth truth | Blender float-Z OpenEXR → radial-range NPY; независимая аналитическая проверка плоскостей, 3D-примитивов (сфера, куб) и разрывов силуэта; discrete semantics, ground/raised markers и exploratory E-STITCH-01 v2 matrix; [[validation/DEPTH_VISIBILITY_TRUTH]], [[validation/E_STITCH_01_V2]] | Независимая оценка multi-band/graph-cut по object/edge truth, image-quality oracle для точной Blender сцены, маска кузова и validation на последовательностях |
| Слияние и поверхности | Исправленный 84-case offline matrix повторён на checksum-verified RGB/depth fixture; [[validation/E_STITCH_01_V2]] | Один синтетический кадр, нет independent object/edge truth и holdout; бинарные cuts только для двух камер, остальное — centrality fallback. Temporal tool по-прежнему не применяет pose; Scene Truth RGB не совпадает с Blender street capture. Нужны независимые сцены/clips, глобальная политика multi-camera overlaps и GPU/target timing |
| Калибровка | Synthetic XYZ/OpenCV solver, mount-error и board-survey sweeps, Blender image-derived шахматная доска, server calibration job с held-out gate, кодовые эксперименты KB/RadTan/Omni и Joint Bundle Adjustment; [[research/CALIBRATION]], [[research/IMAGE_CALIBRATION]], [[research/MOUNT_CALIBRATION]] | `real_data_calibration_evaluation.py` лишь генерирует синтетические кадры и случайный jitter: OpenCV detector/solver не вызываются, `num_trials` не используется, а gate thresholds hard-coded. Клиент также присылает обе выборки gate; сервер не подтверждает их независимость. Модели/ChArUco имеют упрощённые ground-truth предпосылки. Нужны provenance/holdout, real image tests и false-accept/false-reject оценка |
| Сервер и библиотека | SV01 описан; `sv-client-lib` Unix/TCP; сервер хранит config и безопасно применяет calibration; `sv-client`, `svctl`, `sv-simulator` используют library | Серверный runtime config API типизирован не полностью; accept/render path односессионный; нет полноценной multi-session, subscriptions и optional-final path |
| GUI | Автомобильный Qt Quick UI имеет управление ракурсом/масштабом и server state/timing/camera information; laptop simulator имеет Unix/TCP discovery/connection, diagnostics и keyboard driving preview | Qt UI пока не управляет общим server config; Aurora integration/device acceptance; simulator не объединяет все экспериментальные workflows и не подключён как live-world producer |
| Источники и transport | Replay, 4 bounded Unix/TCP virtual-camera endpoints, producer reconnect, V4L2 adapter, fault tests | V4L2 не проверен на физических устройствах; нет multi-host clock/skew/thermal trials; UDP отсутствует |
| Aurora packaging | Подготовлены CPU/GPU spec и Linux RPM checks; CMake dependency strategy и package instructions описаны | SDK target dependencies/ABI, `mb2 installdeps`, target rpm-validator/install/launch и аппаратный baseline не проверены; Aurora device/SDK не был предоставлен |
| Диплом | Подготовлены главы 1–5, ссылки, формулы, сравнительные таблицы, схемы и графики по существующим опытам | Главы остаются рабочей редакцией; необходимо добавить будущие измерения и провести итоговую сверку/редактуру с руководителем |

## Последние проверки

- GUI/simulator изменения: сборка Qt targets, `qmllint`, offscreen world smoke; полный CTest прошёл 22/22 для той ревизии.
- Depth truth / visibility: `python3 tests/test_blender_fixture.py` — 16/16; `ctest --test-dir build -R blender_fixture --output-on-failure` — 1/1. Analytic-plane, 3D primitives (sphere, cube, silhouettes), semantic conversion, 30-case depth-visibility screen и corrected 84-case E-STITCH-01 matrix — [[validation/DEPTH_VISIBILITY_TRUTH]], [[validation/STITCH_VISIBILITY]], [[validation/E_STITCH_01_V2]].
- Blender smoke capture: один синтетический кадр, 20 OpenEXR depth faces (4 камеры × 5 направлений), четыре fisheye NPY карты. Это проверка экспортной цепочки и формата, не точности depth и не качества fusion.
- Состояние source tree на момент аудируемого диапазона: `master` опережал `origin/master` на пять пользовательских commits. Затем добавлен audit commit `547b7a6`; исправления первой 84-case matrix зафиксированы в `6528bd7`. Ни эти commits, ни host tests не являются подтверждением target/Aurora acceptance.

## Аудит коммитов `ee1bcc5e..HEAD` (09.10.2026)

В запрошенном диапазоне `ee1bcc5e23ed563d778e91ebd72d8c802ecf6d00..fea96e3` — 17 коммитов; на тот момент локальная ветка содержала пять пользовательских commits сверх `origin/master`. Изменения инфраструктуры и серверной калибровки в основном соответствовали целям: CMake искал системные GLM/OpenCV до pinned fallback, fallback выключен для `SV_AURORA`; job ownership/cancel, held-out gate, stale revision и сохранение старого renderer реализованы в коде и сопровождаются C++ тестами. Drivable world добавлен как визуальный preview, но не как источник четырёх live frames — это ограничение уже остаётся в TODO.

Исследовательская часть коммитов `913d62c`, `43d00f8`, `ddb40c5` и `9955ab1` добавила исполняемые скрипты и тесты, однако их отчёты/выводы были объявлены завершёнными раньше, чем проверена валидность метрик. В частности:

| Коммит/изменение | Что фактически появилось | Что не подтверждено и добавлено в [[../TODO]] |
|---|---|---|
| `913d62c` — E-STITCH-01 | Offline matrix из 84 комбинаций carrier/view/mode | Реализация под названием graph-cut не решает graph energy; color-cost сокращается из обеих сторон сравнения. Seam score — Sobel magnitude вместо скачка по шву; ghost score измеряет исходные views, не результат fusion. Нужны корректные метрики и повтор на holdout |
| `43d00f8` — image/temporal oracle | Синтетический analytic scene oracle и генератор короткой серии | Временной тест рассчитывает `T_veh`, но не применяет его; швы определяются из статичных geometry weights. Нулевое смещение не доказывает временную стабильность |
| `ddb40c5` / `9955ab1` — calibration studies | Модельные/Joint-BA sweeps и генерация synthetic chessboard images | «Real-data» тест не запускает OpenCV detector или solver, измерения синтезируются через заранее заданный jitter, `num_trials` не используется, а gate thresholds hard-coded. Не доказывает detector performance или физические пороги |
| `fea96e3` — очистка TODO | Удалены уже выполненные повторы | Удалён открытый пункт о калибровке на реальных снимках, хотя добавленный в `9955ab1` прогон синтетический. Пункт восстановлен с более точным критерием приёмки |

Выборочные автоматические проверки коммитов прошли: `ctest --test-dir build -R 'stitch_fusion|image_quality_oracle|comprehensive_calibration|real_data_calibration' --output-on-failure` — 4/4. Они проверяют запуск/структуру результатов, но не обнаруживают перечисленные методические дефекты; поэтому эти pass не закрывают соответствующие исследовательские пункты. Полный порядок исправления и условия зачёта находятся в [[../TODO]].

## Исправления после аудита диапазона

Аудит выше фиксирует состояние исследовательских коммитов на момент диапазона `ee1bcc5e..fea96e3`. После него работа продолжена: fake distance heuristic заменена бинарным max-flow/min-cut в попарных overlap областях, метрики считают output color step и требуют fused-edge responses, а 84-case matrix пересчитана. Проверки `stitch_fusion` и `e_stitch_inputs` проходят 2/2. Входные RGB/depth данные теперь включены в `tests/data/e_stitch_01_v1`, скрипт проверяет checksums, порядок/ID камер, размеры и синхронность. Подробные числа и пределы вывода: [[../validation/E_STITCH_01_V2]].

Пересчёт не закрывает исходные проблемы качества: на одном кадре медиана p95 CIE76 у `graph_cut_seam` выше, чем у `edge_feather`, но этот score смешивает seam и scene-edge effects; CPU offline timing также не предсказывает GPU renderer. Поэтому TODO сохраняет проверку метрик по независимым annotations/holdout, exact scene oracle, temporal rig motion и корректность cuts в 3–4 camera overlaps.

## Документы, отвечающие за актуальный план

- Текущие незакрытые задачи и их приоритет: [[../TODO]].
- Зависимости этапов и критерии проекта: [[ROADMAP]].
- Кодовые факты и воспроизводимые измерения: [[../prototype/STATUS]], [[../validation/ACCEPTANCE]], [[../research/EXPERIMENTS]].
- Полный scope сшивки: [[../research/PROJECTION_AND_STITCHING]].
- Подтверждённый SV01 wire contract и gaps: [[../engineering/PROTOCOL_IMPLEMENTED]].
- Целевой Aurora package/runtime workflow: [[../engineering/AURORA]].

Задача считается завершённой только при совпадении реализации с документированным критерием и наличии воспроизводимого свидетельства. Условные требования будущей архитектуры в `architecture/CLIENT_SERVER_MODEL.md` не означают, что они уже реализованы.

### Парный multi-frame truth, 10.10.2026

Старый temporal sine-shift генератор заменён проверяемым входом с реальным перемещением камер и exact-scene direct Blender RGB. Компактный fixture, object-ID ROI, residual-change/weight-boundary metrics и негативные контроли: [[validation/PAIRED_STITCH_TEMPORAL]]. Причина исходной temporal ошибки устранена; критерий качества остаётся открытым: один короткий клип не заменяет holdout scenes, динамические объекты/экспозицию, matched visibility и object-level ghost truth.

### Истинная source visibility и sampling ablation

[[validation/STITCH_RESOLUTION_VISIBILITY]] фиксирует сравнение 64/256 исходных кубических граней с идентичными decoded direct RGB/object/visibility truth. Найден и исправлен дефект ray casts: скрытые для RGB модели камер заслоняли геометрию в truth. Blender negative control различает visible/hidden helper; текущие object-ID ROI не приравнивать к историческим картам первой paired серии. Source visibility теперь измеряется отдельно от carrier validity, но object-correspondence ghost rate и independent multi-scene confirmation остаются открытыми.

Convergence 256→512 и три scene variants с nominal/static-bias/front-jump exposure выполнены: [[validation/STITCH_CONVERGENCE_ROBUSTNESS]]. Исправлен недостаток вариативности генератора: прежние seeds оставляли ближайшие столбики неподвижными. Результаты не закрывают object-correspondence metrics, real detector/calibration, dynamic clips и target acceptance; критерии перечислены в [[ROADMAP#Критерии готовности исследовательского заключения]].
