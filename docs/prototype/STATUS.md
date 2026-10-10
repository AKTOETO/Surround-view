# Состояние прототипа и границы подтверждения

Редакция 09.10.2026, Linux-профиль 0.6.x. Это карта фактически реализованного прототипа, а не объявление завершения всех MUST из [[requirements/SYSTEM]]. Рабочий профиль — `linux-prototype-v1`; его JSON намеренно уже полной проектной схемы из [[requirements/CONFIGURATION]]. Совпадение `schema_version: 1` означает версию этого явно именованного профиля, а не взаимозаменяемость всех полей прежнего YAML-примера.

## Реализовано и проверено

| Работа | Свидетельство | Оставшееся ограничение |
|---|---|---|
| Проекция fisheye, V→C, GLM view, плоскость/bowl | Реальный OpenCV; 8000 NumPy и 8192 native analytic/OpenCV points | Нет CPU-растеризатора полного изображения |
| Валидация JSON | Отрицательные тесты единиц, версии, матриц, камер, поверхности и монотонности | Нет полной JSON Schema нормативного профиля, произвольных масок и resize/crop |
| EGL/GLES 3, четыре текстуры, linear-RGB blending и ego-модель | Mesa/RTX; GPU projection, асимметричный RGBA marker, optional valid draw query | Final readback синхронный; таймер зависит от расширения |
| Ограниченные очереди и синхронизация | C++-проверки возраста, skew, дубликатов, очередей | Replay/socket и начальный OpenCV/V4L2 source; физические камеры, аппаратные timestamps и распределённые часы не проверены |
| Калибровка intrinsics/extrinsics | Known-XYZ solver, независимая validation; native chessboard image detector; Joint Bundle Adjustment и сравнение семейств дисторсии; [[research/CALIBRATION]] | На реальной метрической площадке точность image-derived workflow не измерена; detector order требует контрольной метки |
| OpenCV detector и image-based intrinsics | `vision_tools`, synthetic projected images; hashes/disjoint train/validation | Held-out board pose fitted; нет real-camera испытаний и внешней привязки |
| OpenCV VideoCapture | Recorder для файлов и начальный realtime server adapter `source.type=camera` | Host delivery times не sensor timestamps; реальные камеры и остановка на разных драйверах ещё не проверены |
| Blender 3D street fixture | Разнесённые centers, scripted motion, 4 × fisheye RGB, hashes/poses; replay/server smoke; EXR depth → radial-range truth; independent analytic-plane, 3D curved/box primitives (sphere, cube), silhouette step edges, discrete semantics, ground/raised markers | Упрощённый автомобиль; нет live Blender render и физической камеры |
| Фотографическая street-demo | CC0 panorama, три actual GLES ракурса в главе 3 | Общий оптический центр, нет реального параллакса/калибровочной истины |
| Купол, цилиндр и куб с полом | Shared containment, outward meshes; GPU coverage для 36 ракурсов, без отверстий геометрии | Это носители проекции; depth truth пока не используется при рендере, visibility masks и качество на независимых сценах не оценены |
| Три fusion-режима и расширенные baseline | Native RGB/weights/coverage oracles; 84-case E-STITCH-01 matrix across 6 carriers; Image-Quality Oracle и маска кузова; [[engineering/RENDERING]], [[research/PROJECTION_AND_STITCHING]], [[validation/STITCH_VISIBILITY]], [[validation/IMAGE_QUALITY_ORACLE]] | Photometric correction на динамических клипах |
| Сервисная диагностика задней камеры | Заданный поворот, известные точки, INDETERMINATE | Нет анализа признаков перекрытий и статистики реальных ложных тревог |
| Headless research runner | Общий C++ service + `svctl research`; paused-frame blocks через sv-client-lib, reports/hash/timing summaries, cancel/failure restore на Unix/TCP | Пока без GUI, sequence/reset, independent truth и серверного lease/watchdog; [[validation/RESEARCH_RUNTIME]] |
| Runtime fusion control | `fusion_runtime_v1`: catalog и typed client API, revision-checked temporary переключение трёх GPU-режимов/diagnostics/параметров; paused-input integration проверяет apply/reject/restore без upload/rebuild | Нет общего ConfigService, lease, auto-restore и scenario runner; offline advanced modes ещё не перенесены |
| Сервер с Asio и отдельным EGL-потоком | Unix/TCP listeners по config, state/pause/step, timeout/reconnect, decode/mesh counters | Один клиент и один ожидающий release; V4L2 adapter есть, но реальные устройства и driver shutdown не проверены |
| Универсальная клиентская библиотека | GUI/headless, Unix/TCP localhost, installed CMake consumer, RGBA ownership, deadlines и restart/reconnect; calibration jobs доступны через generic command API | Нет UDP, two-host испытания и типизированного доменного API для общей config/calibration/source management |
| Qt Quick desktop-клиент через библиотеку | Qt 6.11.2/5.15.19 offscreen; исправлен teardown Bridge | Не AuroraApp/Silica application; display latency не измерена |
| SV01 framing и две очереди | Однобайтовое/объединённое чтение, malformed, control/data | Это подмножество протокола, не полная приёмка PRO-F-001…011 |
| Исторические серии 0.1.0 и графики | [[prototype/MEASUREMENTS]], `run_experiments.py` | Короткие synthetic опыты; thermal/memory/display latency в этой серии не измерены |
| Native hardware-comparison suite | 25 pass в коротком smoke; десять render-вариантов включают оболочки и fusion | Полные v0.6 RTX/Mesa baselines сохранены в [[validation/SOURCES_SMOKE]]; нет target hardware/long-run/VRAM/IPC UI |
| Offline RPM/spec и installed resources | Оба Linux RPM 0.6.0, payload core/source fixture/report/scene/GPU и отдельный CMake client consumer; Qt/GLSL embedded | Aurora ABI/dependencies/validator/signing/installation не проверены |

## Отображение проектной архитектуры на код

| Проектная часть | Текущая реализация |
|---|---|
| `sv-core` | CMake target и `src/core/`; без Qt/Asio |
| `sv-vision` | Настоящий OpenCV: projection/images/board/intrinsics |
| `sv-validation` | CPU criteria, SHA-256, statistics, system passport и report |
| GPU-часть ядра | Выделенный target `sv-render`, `src/render/` |
| `sv-gpu-validation` | Общие native/CTest орacles fusion и enclosure coverage |
| `sv-sources` | `FrameSource`, replay decode worker либо Asio worker четырёх Unix/TCP camera inputs |
| `sv-server` | CLI, клиентский Asio worker, source worker и основной render-поток |
| `sv-bench` | Изолированный рендер, эффективная конфигурация, численная проверка |
| `sv-platform-test` | Native критерии и portable JSON/Markdown для устройства |
| `sv-client-lib` / `sv-wire` | Общий Asio API/codec без Qt/OpenCV/GPU; installed `sv::client` |
| `sv-client` | Desktop Qt/QML через библиотеку, QQuickImageProvider |
| `sv-configurator` | `tools/configurator.py`, Python/NumPy CLI |
| `sv-simulator` | Qt 6 GUI в `examples/sv-simulator/`: Unix/TCP, ограниченный IPC discovery, серверные diagnostics и визуальный keyboard-driven Qt Quick 3D driving preview. `tools/simulator.py`/`tools/blender/` отдельно создают offline PPM/manifest и Blender-сцену |
| `sv-calibrate`, `sv-capture`, `sv-scene` | OpenCV detector/fitter, recorder и фотографический generator |

QQuickImageProvider заменяет проектный QQuickItem/QSGTexture. Сервер имеет клиентский io_context-runner, отдельный source worker и одного GL-владельца; общий worker pool отсутствует. Native qualification проверяет все manifest hashes, обычный replay-loader — ID/формат. Эти различия остаются явными. Основная инструкция по коду: [[engineering/BUILD]], [[engineering/USAGE]], [[engineering/AURORA]], [[engineering/PLATFORM_TEST]].

## Следующие обязательные работы

Приоритетный список с критериями завершения находится в корневом [[../TODO]]. Ключевые оставшиеся условия: реальные калибровочные наблюдения и проверка V4L2; независимая validation sphere/cube/semantic depth truth; image-based сравнение seam/ghosting/temporal quality; typed ConfigService и многосессионный server path; two-host и Aurora target acceptance.

Подробный план остаётся в [[planning/ROADMAP]], исполняемый список начала — в корневом `TODO.md`. Текст глав — [[diploma/README]].

## Уточнение следующего этапа 05.10.2026

Запрошены live-источники `/dev/video*`, виртуальные камеры через отдельные сокеты, удалённый клиент/конфигуратор и 3D-сцена. Есть offline Blender-улица с движением по траектории и отдельный keyboard-driven Qt Quick 3D preview в `sv-simulator`; последний пока визуальный и не передаёт кадры серверу. Готовые кадры Blender передаются через replay-manifest или Unix/TCP producer. `sv-capture` записывает камеры отдельно, `sv-scene` генерирует фотопанораму. Строгий parser принимает plane/bowl/dome/cylinder/cube, fusion и Unix/TCP connections; UDP явно отвергается. Порядок продолжения — [[planning/ROADMAP#Следующий этап: источники, удалённое управление и 3D-окружение]].

07.10.2026 выполнен первый Blender image-derived extrinsics experiment: четыре кадра на камеру, native OpenCV chessboard detection, train/held-out split, offline candidate fit и report/figure. Детали, численные результаты и границы synthetic evidence: [[validation/IMAGE_CALIBRATION]], [[research/IMAGE_CALIBRATION]]. Это не runtime calibration в сервере и не физическая приёмка камеры.

В 0.5.0 `sv-client` использует `sv-client-lib` для Unix/TCP. Проверка между двумя машинами остаётся открытой. Контракт — [[requirements/CLIENT#Клиентская библиотека sv-client-lib (план)]], решение — [[architecture/DECISIONS|ADR-012]]. Библиотека и connections вошли в код/config 0.5.0; исторические baselines не изменены.

Рабочий API 0.5.0: [[engineering/CLIENT_LIBRARY]]. Полный аудит планов: [[planning/AUDIT]]. Новые socket-проверки не подтверждают завершение всего M7 или целевого M9.

## Источники 0.6.0

Вход выбирается из config: replay worker, четыре Unix/TCP socket cameras или четыре локальных V4L2 устройства. `tools/producer.py` передаёт verified recording четырьмя независимыми потоками; GL только забирает готовые frames. Пауза имеет source completion barrier; socket-входы на паузе читаются и отбрасываются, step недоступен. Проверки 12/12 Release и 8/8 CPU ASan/UBSan относятся к replay/socket baseline [[validation/SOURCES_SMOKE]]; новая lifecycle проверка camera source не подключает физические сенсоры. Полный формат и ограничения — [[engineering/SOURCES]].

## Восстановление host producer

`tools/producer.py` автоматически восстанавливает Unix/TCP camera connections с независимыми retry budgets, пропускает просроченные кадры и сохраняет JSON/Markdown отчёт, в том числе при SIGINT/SIGTERM. Проверка настоящего server restart, blocked handshake и slow receiver — [[validation/PRODUCER_RECOVERY]]. Это host-расширение 0.6.0; C++ API, native fingerprint и проверенные RPM не изменены. Аппаратный backend, sensor clocks и передача live кадров из driving preview остаются открытыми.


## Dense CPU-reference: выполненная часть исследования

06.10.2026 добавлены `tools/reference.py` и `tools/compare_reference.py`: аналитические пересечения plane/bowl/dome-floor/cylinder/cube, независимый fisheye через atan2, билинейная выборка и три fusion-режима. Сохранены 30 offline случаев, coverage/weights/point maps, SHA-256 и иллюстрация. Десять новых CPU проверок и 13 CTest-групп проходят. [[engineering/RENDERING#Независимый аналитический CPU-эталон]], [[validation/ANALYTIC_REFERENCE]].

Это закрывает подготовку плотного геометрического эталона в E-STITCH-01. Дополнительно `tools/validate_depth_plane.py` и `tools/validate_depth_primitives.py` проверяют cube-depth conversion на аналитических плоскостях, 3D-сферах/кубах и силуэтных ступенях, `tools/semantics.py` задаёт категориальную разметку, а `tools/compare_visibility.py` измеряет 30 случаев по Blender RGB/depth frame. Следующие работы — graph-cut/multi-band implementations, маска кузова и количественное CPU/GPU сравнение E-STITCH-01 на одинаковых holdout scenes/clips. Исторические native baselines/RPM относятся к сохранённым ревизиям; host-инструмент не добавляет критерии в native suite.


## Приоритетная архитектура 07.10.2026

Зафиксирована целевая модель [[architecture/CLIENT_SERVER_MODEL]]: server-owned config, несколько сессий, per-session view/subscriptions, intermediate products, optional final output и подробные pipeline spans. Это ещё не текущие capabilities. GUI перенесён в `examples/sv-client`, есть touch-oriented controls и server diagnostics; `svctl` предоставляет текущие операции без Qt. Installed consumer находится в `tests/fixtures/client-consumer`. `sv-simulator` — Qt 6 GUI с endpoint discovery, telemetry и keyboard-driven visual world; генерация live frames и calibration observations, а также редактор камер ещё не реализованы.

`ConfigStore` атомарно хранит revisioned config; calibration jobs привязаны к control session, имеют validation gate, cancel/ownership, stale revision rejection и строят renderer до atomic persistence/apply. Пороговые значения 3 px RMSE / 8 px max предварительны и требуют физической серии. Это не typed ConfigService: общие read/update/validate/status API, frame-boundary transaction и idempotent retry остаются открытыми. Сервер односессионный, публикует legacy final output; multi-session, per-session subscriptions/intermediate products и полный trace плановые. Полный список — в корневом TODO. Protobuf/C++20 не добавлены; причины и условия пересмотра — [[architecture/DECISIONS]].

Результаты первого этапа: 14/14 Release и 10/10 CPU ASan/UBSan, Qt 5 build/offscreen smoke, CLI Unix/TCP и installed consumer — [[validation/CLIENT_RESTRUCTURE]].

## Дополнение 07.10.2026: монтаж и native extrinsics

Добавлены deterministic Blender world rules, yaw/pitch/slide с per-camera overrides и сохранённый мир без LFS. `sv-vision` предоставляет четыре OpenCV pose pipeline; offline `sv-calibrate extrinsics` экспортирует candidate с provenance. Configurator запускает 120 четырёхкамерных trials с независимой validation, failure counts и actual GLES до/после. Протокол и результаты — [[research/MOUNT_CALIBRATION]], [[validation/MOUNT_CALIBRATION]]. Позднее добавлены image-derived chessboard observations и серверный quality gate на отложенных наблюдениях; это не заменяет joint calibration и аппаратную оценку. Полноценный ConfigService и GUI-редактор мира остаются нереализованными.

## Дополнение 09.10.2026: серверные операции и документация

`ConfigStore` атомарно сохраняет валидные изменения и сохраняет активный snapshot при ошибке записи; `update_if_revision` проверяет ожидаемую revision под тем же lock, что и persistence, а `snapshot()` возвращает согласованные config/revision. `CalibrationJobManager` выполняет асинхронные задания и требует отдельные validation observations; jobs привязаны к control-session, доступны статус/отмена/apply. При закрытии control connection все её jobs отменяются; running OpenCV вызов завершается, но результат отбрасывается. Renderer строится до persistence, затем пара config/renderer переключается только после успешной записи с ожидаемой revision. CTest проверяет stale update rejection, принятие точного fit, отказ плохого held-out fit, изоляцию по session ID и отмену. Открыты подбор порогов на реальных данных, многосессионный ConfigService и применение всех runtime-настроек. Статус клиентского GUI и доступные команды описаны в [[engineering/USAGE]] и `examples/sv-simulator/README.md`.

## Server calibration split validation — 10.10.2026

`CalibrationJobManager` и wire `calibrate` требуют provenance. Общий validator отклоняет duplicate IDs, общие train/validation frames, exact XYZ/UV copies и non-finite данные до enqueue. В job status сохранены dataset ID/split counts и явно ограниченная validation policy. 11 GTest cases и настоящий Unix calibration request/status test дополняют прежние ownership/gate/revision/cancellation проверки. Это проверка клиентских деклараций/точных копий, не verified acquisition или статистическая независимость. Контракт миграции: [[engineering/PROTOCOL_IMPLEMENTED]]; свидетельства: [[validation/CALIBRATION_PROVENANCE]].
