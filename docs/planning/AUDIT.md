# Актуальный аудит документации и реализации

Дата сверки: 09.10.2026. Проверены рабочие инструкции, план, TODO, требования, протокол, приложения, Blender capture/converter, исследования и подтверждающие тестовые отчёты. Исторические validation reports описывают ровно ту revision, что указана в самом отчёте; их нельзя трактовать как измерения текущего HEAD.

## Состояние по главным этапам

| Направление | Подтверждено в проекте | Что остаётся открытым |
|---|---|---|
| Математика, GPU и четыре камеры | Независимые CPU-модели, OpenCV projection/calibration tools, GLES renderer, plane/bowl/dome-floor/cylinder/cube, hard/edge/angular fusion, карты покрытия и веса; доступны RTX/Mesa baselines | Сравнение качества с единым depth/visibility oracle, одинаковые бюджеты и независимые validation scenes |
| Blender стенд | Процедурный мир с автомобилем, разнесёнными центрами камер, заданными позами/траекторией; replay/socket producer; Qt Quick 3D driving preview | Preview только визуальный: он не отправляет 4 синхронных live-кадра и не создаёт calibration observations. Нет physics/полноценной модели автомобиля |
| Depth truth | Blender float-Z OpenEXR → radial-range NPY экспортируется и хэшируется; smoke-серия и тесты описаны в [[validation/DEPTH_VISIBILITY_TRUTH]] | Аналитическая оценка погрешности, semantic/object IDs, body visibility и несколько validation scenes |
| Слияние и поверхности | Обзор методов, 70-case first-frame screening, пять носителей/три дешёвых режима | Метрики seam/ghosting/temporal, photometric policy, graph-cut/multi-band, равный triangle/memory budget, детальный разбор Burger |
| Калибровка | Synthetic XYZ/OpenCV solver, mount-error и board-survey sweeps, Blender image-derived шахматная доска, server calibration job с held-out gate, ownership/cancel и revision-checked save | Реальные measured observations, широкий обзор методов, расширенные nuisance sweeps и физическая настройка порогов 3/8 px |
| Сервер и библиотека | SV01 описан; `sv-client-lib` Unix/TCP; сервер хранит config и безопасно применяет calibration; `sv-client`, `svctl`, `sv-simulator` используют library | Серверный runtime config API типизирован не полностью; accept/render path односессионный; нет полноценной multi-session, subscriptions и optional-final path |
| GUI | Автомобильный Qt Quick UI имеет управление ракурсом/масштабом и server state/timing/camera information; laptop simulator имеет Unix/TCP discovery/connection, diagnostics и keyboard driving preview | Qt UI пока не управляет общим server config; Aurora integration/device acceptance; simulator не объединяет все экспериментальные workflows и не подключён как live-world producer |
| Источники и transport | Replay, 4 bounded Unix/TCP virtual-camera endpoints, producer reconnect, V4L2 adapter, fault tests | V4L2 не проверен на физических устройствах; нет multi-host clock/skew/thermal trials; UDP отсутствует |
| Aurora packaging | Подготовлены CPU/GPU spec и Linux RPM checks; CMake dependency strategy и package instructions описаны | SDK target dependencies/ABI, `mb2 installdeps`, target rpm-validator/install/launch и аппаратный baseline не проверены; Aurora device/SDK не был предоставлен |
| Диплом | Подготовлены главы 1–5, ссылки, формулы, сравнительные таблицы, схемы и графики по существующим опытам | Главы остаются рабочей редакцией; необходимо добавить будущие измерения и провести итоговую сверку/редактуру с руководителем |

## Последние проверки

- GUI/simulator изменения: сборка Qt targets, `qmllint`, offscreen world smoke; полный CTest прошёл 22/22 для той ревизии.
- Depth truth: `python3 tests/test_blender_fixture.py` — 10/10; `ctest --test-dir build -R blender_fixture --output-on-failure` — 1/1.
- Blender smoke capture: один синтетический кадр, 20 OpenEXR depth faces (4 камеры × 5 направлений), четыре fisheye NPY карты. Это проверка экспортной цепочки и формата, не точности depth и не качества fusion.
- Состояние source tree: `master` опережает `origin/master`; commits локальные и не являются подтверждением target/Aurora acceptance.

## Документы, отвечающие за актуальный план

- Текущие незакрытые задачи и их приоритет: [[../TODO]].
- Зависимости этапов и критерии проекта: [[ROADMAP]].
- Кодовые факты и воспроизводимые измерения: [[../prototype/STATUS]], [[../validation/ACCEPTANCE]], [[../research/EXPERIMENTS]].
- Полный scope сшивки: [[../research/PROJECTION_AND_STITCHING]].
- Подтверждённый SV01 wire contract и gaps: [[../engineering/PROTOCOL_IMPLEMENTED]].
- Целевой Aurora package/runtime workflow: [[../engineering/AURORA]].

Задача считается завершённой только при совпадении реализации с документированным критерием и наличии воспроизводимого свидетельства. Условные требования будущей архитектуры в `architecture/CLIENT_SERVER_MODEL.md` не означают, что они уже реализованы.
