# Состояние прототипа и границы подтверждения

Дополнение 13.10.2026: opt-in native local-support mask разделяет eligible camera count, positive local weights и actual output path; 11 новых tests, 21/21 Mesa и NVIDIA, выбранная регрессия9/9. RGBA unchanged, wire delivery ещё не реализована: [[validation/RENDER_SUPPORT]], §4.79.

Редакция 10.10.2026, Linux-профиль 0.6.x. Это карта фактически реализованного прототипа, а не объявление завершения всех MUST из [[requirements/SYSTEM]]. Рабочий профиль — `linux-prototype-v1`; его JSON намеренно уже полной проектной схемы из [[requirements/CONFIGURATION]]. Совпадение `schema_version: 1` означает версию этого явно именованного профиля, а не взаимозаменяемость всех полей прежнего YAML-примера.

## Реализовано и проверено

Дополнение 11.10.2026: catalog v4 и optional alpha_expansion для двух graph-cut modes. Native weighted-Potts optimizer, convergence telemetry и 512 exhaustive moves проверены; 125/128 small graphs достигли global optimum. Прежний visual screen и24 условия на трёх новых frozen procedural instances дали неоднозначные quality эффекты и дорогой CPU путь (на новых данных +769–958ms median fusion CPU); default binary_pairs сохранён: [[validation/MULTILABEL_SEAM]]. Полная регрессия51/51, 22.54s; Blender MCP снова доступен.

Дополнение 11.10.2026: native runner поддерживает последовательные paused frame sets и RGBA capture, owner lease step и подготовку stale baseline. Проверены cancel/capture failure после перехода; 76 исследовательских условий на прежних Blender fixtures и все шесть settings restores прошли independent audit. Результаты не дают универсального преимущества normalized: [[validation/SERVER_BOUNDARY]]. Полная регрессия — 50/50, 29.12 s.

Дополнение 11.10.2026: Client::release проверяет размер сообщения до post/queue budget, сервер отклоняет release с непустым binary payload закрытием data connection. Проверены synchronous reject, доступность правильного release при насыщении command queue и живой control после malformed data packet: [[validation/CLIENT_SMOKE]].

Дополнение 11.10.2026: parser калибровочных команд принимает конечные XYZ/UV в integer/double представлении, camera_id проверяет до сужения типа. Устранён воспроизведённый дефект выбора другой камеры по большому ID. Native boundary tests и два эквивалентных wire calibration requests проверены; полная регрессия 50/50: [[validation/CALIBRATION_PROVENANCE]].

Текущий SV01 описан каталогом 18 команд и 11 сценариев с условиями отказа и шестью диаграммами: [[engineering/PROTOCOL_SCENARIOS]]. Исправлен wire order конкурентных command callers в библиотеке; новый тест восьми отправителей/256 команд и полная регрессия 49/49 прошли: [[validation/CLIENT_SMOKE]]. Это не поддержка нескольких клиентских сессий сервером.

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
| Семь fusion-режимов и расширенные baseline | Native RGB/weights/coverage oracles; 84-case E-STITCH-01 matrix across 6 carriers; Image-Quality Oracle и маска кузова; [[engineering/RENDERING]], [[research/PROJECTION_AND_STITCHING]], [[validation/STITCH_VISIBILITY]], [[validation/IMAGE_QUALITY_ORACLE]] | Photometric correction на динамических клипах |
| Сервисная диагностика задней камеры | Заданный поворот, известные точки, INDETERMINATE | Нет анализа признаков перекрытий и статистики реальных ложных тревог |
| Headless research runner | Общий C++ service + `svctl research`; sequential paused-frame blocks через sv-client-lib, native RGBA capture, reports/hash/per-frame timing summaries, cancel/failure restore на Unix/TCP | Server lease/watchdog fusion/surface/pause есть; пока без GUI сценариев, cursor/history reset и durable checkpoint; проверка независимого direct-view truth на прежних fixtures: [[validation/SERVER_BOUNDARY]]; [[validation/RESEARCH_RUNTIME]] |
| Runtime fusion control | `fusion_runtime_v1`: catalog и typed client API, revision-checked temporary переключение трёх GPU и четырёх GLES/CPU режимов/diagnostics/параметров, пяти носителей; paused-input integration проверяет apply/reject/restore без upload; fusion не перестраивает mesh | Нет общего ConfigService; runner и auto-restore fusion/surface/pause доступны при experiment lease; burger-like carrier остаётся offline |
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

Native fusion/parity и runtime geometry: [[validation/NATIVE_FUSION]]. Binary cuts не являются global multilabel solver; серверные smoke/parity не закрывают исследование качества на holdout и целевом устройстве.

Полный hybrid raster oracle (60 cases), analytic plane sampling и 12 SV01 frames добавлены; исправлены offline parameter forwarding и pyramid invalid-RGB leakage. 84-case matrix пересчитана, исторические pyramid серии помечены для повторения: [[validation/NATIVE_FUSION]].

## Нативный путь настройки из симулятора

В `sv-simulator` добавлен отдельный C++ `RuntimeSettings` adapter и QML редактор fusion/surface: чтение state/catalog, полный JSON-снимок, revision-aware apply через типизированные методы `sv-client-lib`, отображение server ACK/rejection. Изменения временные; записи server config из клиента нет. Черновики привязаны к загруженной ревизии и сбрасываются при переподключении. Это не полный ConfigService и не GUI сценариев. Политика дальнейшего переноса продуктовых Python workflows: [[architecture/CLIENT_SERVER_MODEL]].

Исправления GUI runtime: локальные Error с command_id завершают pending без ожидания ACK; чужой ID игнорируется. В обоих Qt клиентах заполненный frame budget больше не удаляет ACK/ошибки/lifecycle; отброшенные кадры освобождаются. Публикация weak reference в callback защищена mutex. Поле ревизии конфигурации теперь использует config_revision. Новый C++ regression `simulator_runtime_errors` и ограничения overload проверки описаны в [[validation/RESEARCH_RUNTIME]].

Release submission в `sv-client-lib` теперь имеет отдельный bounded budget: насыщение очереди команд больше не отказывает первому RELEASE. Новый TCP mock regression сначала воспроизвёл дефект, затем прошёл. Оба GUI очищают прежние кадры/метрики при потере сессии; симулятор очищает implicit calibration job и pending tracking. CLI endpoint отражается в панели, недопустимый ввод не закрывает старое соединение; discovery пропускает пути длиннее 80 UTF-8 bytes. Событийные unit tests и границы подтверждения: [[validation/RESEARCH_RUNTIME]].

Image URL обоих GUI теперь включает session_id/frame_id. Тест автомобильного adapter проверяет поздний QML callback при совпадающем frame_id в новой сессии; present_submit не приписывается старому URL.

Обработка ответов клиента усилена: базовые поля ACK проверяются до завершения pending command; повреждённый ответ не скрывает неизвестный исход команды. Оба Qt-адаптера перехватывают ошибки обработки сообщений и очищают частично обновлённое состояние вместо выхода исключения в Qt event loop. Для восстановления симулятора требуется явное подключение, автомобильного клиента — перезапуск; обычный transport reconnect сохранён. JSON integer-valued telemetry поддерживается. Подтверждение и ограничения: [[validation/RESEARCH_RUNTIME]].

Повторены прежние resolution/convergence/exposure/static-object/moving-object серии для validity_zero_extension_v2: 819 строк условий, входные hashes совпали с прежними, 585 непирамидальных RGB outputs неизменны. Quality tables/рисунки и tracked baseline обновлены; timing сравнение не выполнялось. Это закрывает долг пересчёта, не E-STITCH quality confirmation: [[validation/STITCH_MASK_FOLLOWUP]].

Pyramid fusion дополнен `pyramid_boundary=zero|normalized` (default zero): production C++ реализация normalized support, catalog v3 и runtime переключение через библиотеку/GUI/C++ runner. Контролируемые constant-field/invalid-RGB/single-camera тесты и full RGBA/SV01 parity прошли; 36-sample native paused screen восстановил исходное состояние. Новый вариант не принят как лучший для уличных сцен и не измерен на Авроре: [[validation/PYRAMID_BOUNDARY]].

Controlled mesh-budget follow-up: actual active/resident carrier buffer telemetry, known-answer allocation controls и Unix/TCP forwarding. Native screen120 условий/3 seeds/5 carriers/4 profiles прошёл strict audit;3196–3208 triangles,58392–60168 bytes position/index payload. Полная регрессия51/51,70.45s. Качество зависит от метрики, сравнение full memory/скорости и generalization остаётся открытым: [[validation/CARRIER_BUDGET]].

Within-carrier refinement: coarse/medium/fine,360 audited условий (240 новых,120 hash-pinned reuse),120 paired rows. Форма/input/fusion неизменны; medium ближе к fine в118/120, но independent truth не улучшается монотонно. Product C++ не менялся; object_metrics regression и полный read-only audit прошли: [[validation/CARRIER_REFINEMENT]].

Post-hoc spatial audit360 прежних outputs: independent object-ID/any-camera visibility разделены на ground/other_scene/coded_target × interior/boundary. Historical weighted interior MAE воспроизводится до1e−12; empty strata возвращают null. Выявлены ground dominance89.24–90.70% и отсутствие pole interior; product runtime/прежние baselines не менялись: [[validation/SPATIAL_ROI]].

Native Renderer inspection теперь сохраняет depth-tested carrier region IDs отдельно от projected-camera samples. C++ controls проверяют floor/shell/background/raised bowl, ego, top-left layout и неизменность обычного RGBA. Скомпилированный sv-carrier-probe выполнил15 geometry passes/30 historical ROI intersections: shell только0.203–0.325% historical кадра против42.608–43.944% lateral. Это не новая RGB quality matrix и пока не wire subscription: [[validation/CARRIER_COVERAGE]].

Lateral Blender follow-up завершён: новые direct RGB/object-ID/visibility на трёх recipes/шести poses, все24 fisheye inputs побитно равны прежним;15 fresh servers/1080 samples/120 quality conditions. Common floor/shell metrics используют одинаковый support для всех carriers. Dome лучше cylinder/cube по shell MAE во24/24 парах, но сильные raised-object distortions/низкая coded IoU сохраняются. Product C++ не менялся; новые recipe/driver/audit/фигуры —исследовательские инструменты. Другие families, controlled parallax и аппаратное подтверждение остаются открытыми: [[validation/CARRIER_LATERAL]].

Controlled flat-ground height/range screen завершён:30 fresh servers/240 quality conditions. Target center z0.8/1.4 даёт IoU0 во160/160 условиях несмотря на source chroma observations; visibility и fixed-view clipping ограничивают интерпретацию. Это initial synthetic result, не финальное подтверждение: [[validation/PARALLAX_HEIGHT]], глава4 §4.53.

Conditional source-visible target screen добавлен как независимый исследовательский oracle на240 прежних native outputs: ignored unobserved surface, full-frame ghost penalty, undefined empty-support scores. Full-mask baseline сохранён, все прежние числовые результаты воспроизведены; digests расширенных metric/analyzer сохранены отдельно. Это не новая product feature или новый holdout: [[validation/PARALLAX_VISIBILITY]].

Fully framed two-mount native screen завершён:480 новых quality conditions, minimum full-box margin10.179px, independent direct RGB совпадает между rigs. Raised-object full IoU0 во320 условиях, conditional IoU0 в280/440 определённых,40 undefined сохранены. Normalized улучшает RGB MAE240/240, но object IoU не улучшает. Это новый synthetic controlled follow-up, не natural/target holdout: [[validation/PARALLAX_FRAMED]], диплом §4.55.

Аппаратный provenance уточнён: framed60/height30/lateral15 native catalogs указывают NVIDIA RTX5070Ti/OpenGL ES3, несмотря на LIBGL_ALWAYS_SOFTWARE=1. Fusion CPU и GPU renderer различаются. Framed audit сохраняет actual render_device и отвергает смешанные устройства; historical Mesa label исправлен через validation erratum без замены прежних измерений.

Парный cubemap256→512 follow-up завершён:60 новых server processes/480 пар, direct RGB/ID/visibility/config/poses/binaries совпадают,960 cube faces проверены. Все320 raised overlaps остаются0 при изменении всех480 RGBA hashes;40 conditional undefined сохранены. Ground IoU59 улучшений/101 ухудшение, RGB MAE228/252: монотонного улучшения нет. Output/fisheye-resolution controls выполнены последующими сериями ниже; correspondence остаётся открытым. Эта серия сама по себе не даёт universal geometry attribution: [[validation/PARALLAX_SAMPLING]], диплом §4.56.

Output-resolution sensitivity320×180→640×360 завершена:60 новых servers/480 пар при побитно прежних512px cube/fisheye400 inputs. Все320 raised full IoU остаются0; conditional high теперь определены во всех480 cases, включая40 low-undefined→high0. High8/high32component metrics совпали480/480; normalized near-z080 displacement28.5–33.2% высоты кадра. Truth ray sampling важен для visibility; pipeline pixel-unit support меняется, это не чистая rasterizer ablation. Старый face counter исправлен на960 без numerical/native changes. [[validation/PARALLAX_OUTPUT]], диплом §4.57.

Fisheye400→800 follow-up завершён:60 fresh servers/480 пар при cubemap512/output640×360, intrinsics scaling с прежним angular support. Independent direct RGB/ID/visibility и decoded RGB960 source cube faces приняты побитно; original binaries/NVIDIA backend прежние. Все320 raised full/conditional IoU остаются0; ground30 улучшений/130 ухудшений full IoU, centroid undefined240 сохранены. Повторный paired audit побитно совпал; CTest4/4,5.97s, object_metrics39 tests. Это синтетическая resampling sensitivity, не новый физический сенсор/сходимость/подтверждение Авроры. Отдельный fisheye-resolution control закрыт; native correspondence/triangle-hit и более широкий геометрический/scene sweep остаются открытыми. [[validation/PARALLAX_SENSOR]], диплом §4.58.

Native carrier-position inspection добавлен как opt-in render-thread request и локальный `sv-carrier-probe --positions` export. Перспективно интерполированные координаты actual mesh, CV_32FC3/vehicle metres/top-left; NaN background/ego,3extra RGBA8 passes. GTest6/6 на NVIDIA и Mesa,1343 selected coarse-mesh rays/backend; fixture-specific max axis errors2.268140mm/0.011885mm, не universal bounds. Compatibility screen:360 recorded hashes/280 actual RGBA для40 прежних conditions совпали; expanded hybrid position capture + plane-wire parity прошёл. Baseline audit воспроизводится побитно. Source projection/validity добавлены следующим шагом (диплом §4.60):7/7 GTest на Mesa/NVIDIA и hybrid sample parity. Scene correspondence/triangle identity, contributing support, wire/library channel и отдельный diagnostic overhead остаются открытыми. [[validation/CARRIER_POSITIONS]], диплом §4.59.

Hybrid fusion contribution capture добавлен как opt-in внутренний C++ API: post-smoothing blend weights, normalized per-level coefficients и signed reconstructed camera colors до clamp/composition. GPU-only modes явно отклоняются. Tests включают known-answer weights, reconstruction, empty/reset и nonlocal validity boundary; native probe экспортирует промежуточные матрицы. Scene-surface correspondence, bounded server/library channel и diagnostic overhead остаются открытыми. [[validation/CARRIER_POSITIONS#Расширение: вклад камер после смешивания]], диплом §4.61.

Первый native source-surface correspondence pilot выполнен на одном прежнем кадре edge_feather и пяти carriers: independent target mask178 и visibility воспроизведены, RGBA совпали с archived server baseline hashes. На76 source-visible target pixels (включая48 visible interior) допустимая camera3 выбирает road, а не target; правильная projection carrier point расходится с independent double расчётом менее0.00009px. Механизм geometry/height failure локализован для этого fixture, исправление/обобщение ещё не выполнены. [[validation/SOURCE_CORRESPONDENCE]], диплом §4.62.

Height/second-frame follow-up выполнен:20 native captures с explicit world/vehicle transform, четырьмя known-answer tests и совпадением всех archived frame RGBA hashes. Raised road-selection failure сохраняется после translation; ground-contact box даёт wrong-point hits того же объекта. Planar target, displaced/hybrid attribution, mitigation и server/library diagnostic delivery ещё нужны. [[validation/SOURCE_CORRESPONDENCE_CONTROLS]], диплом §4.63.

Isolated planar geometry control выполнен без нового native render: Blender/analytic oracle на прежних native points/UV. Strict0.1mm/0.01px criteria прошли5/8 exact-floor условий;3 failure сохранены. Оптический residual<0.000082px, общий planar UV gap≤0.011027px. Planar RGB и объяснение raster-position residual остаются открытыми. [[validation/PLANAR_CORRESPONDENCE]], диплом §4.64.

Precision follow-up:10 Mesa native captures с прежним binary/config/input, fixed ROI и independent analytical plane. Strict floor pass NVIDIA5/8, Mesa8/8, camera validity совпала10/10; matrix rounding alone не объясняет residual. Actual triangle/raster attribution и planar RGB ещё открыты; Renderer не изменён. [[validation/PLANAR_PRECISION]], §4.65.

Displaced edge-feather attribution выполнен:10 прежних native captures,52720 RGB-conditioned pixels, independent Blender rays. У51556 pixels source target перекрывает carrier point, тогда как virtual surface — road;1164 point-ray/RGB mismatch сохранены. Color reconstruction max0.784 RGB8 LSB. Нового native renderer/capture нет; hybrid matched capture и mitigation ещё нужны. [[validation/DISPLACED_CORRESPONDENCE]], §4.66.

Matched hybrid pilot завершён:60 conditions на прежних frames/пяти carriers, exact carrier/source parity, unchanged edge fallback. Native API сохраняет signed per-level bands; SciPy reconstruction≤1.49e−8 linear RGB, composition≤1LSB. Raw expected target overlap0 во всех60; mitigation ещё нет. CTest2/2, GTest7/7, correspondence9/9. [[validation/HYBRID_CORRESPONDENCE]], §4.67.

Four-texel diagnostic follow-up выполнен без изменения native renderer:1164 mismatches,1024 neighboring target support/140 upstream RGB-center residual;320 controls,11/11 tests, CTest1/1. [[validation/SOURCE_FOOTPRINT]].

Cube footprint follow-up выполнен без изменения product renderer:140 residual pixels,135 cube-center/5 area-only support;256 source RGB texel reconstruct0LSB;13 tests/CTest1/1, identical requests и reaudit. [[validation/CUBE_FOOTPRINT]].

Новые hybrid proxy coordinates размечены:547 unique frame/carrier pixels/1227 condition memberships;640 archive anchors exact. Local source center target support0,698 texel support/529 без него;15 tests/CTest1/1, побитный audit. Product renderer не менялся. [[validation/HYBRID_DOMAIN]].

Native rectangular carrier поддерживает optional reference_height_m[0,5]m/default0 через configure_surface/surface_catalog.8-condition supplied-height pilot показал target/road tradeoff; general mitigation не готова. Full build/CTest7/7,carrier GTest8/8 на Mesa/NVIDIA,independent16/16. [[validation/REFERENCE_HEIGHT]].

Native optional local_support_patch добавлен для rectangular carrier: vehicle-frame plateau/transition, runtime configure_surface/catalog, строгие bounds/clearance и restoration.8-condition supplied-geometry pilot: top face возвращает все expected proxy hits, road MAE+0.41–0.71%, но displaced copies остаются. CTest7/7,carrier GTest9/9 Mesa/NVIDIA,independent17/17. Это известная геометрия, не RGB depth estimator; первый следующий supplied-box source visibility control описан в [[validation/SOURCE_VISIBILITY]]. [[validation/LOCAL_SUPPORT]].

Native optional source_occlusion_box для rectangular carrier: one opaque vehicle-frame AABB, metric endpoint bias, shared ordinary/hybrid/inspection predicate,status7, configure_surface/catalog.8 matched supplied-geometry captures:outside local4922→198/5449→56, source-visible hits76/76 и69/69 сохранены; target без источника78/103. Full build/CTest7/7,carrier GTest10/10 Mesa/NVIDIA,independent18/18,0 frozen oracle mismatches. Known scene input, не RGB depth/full-scene visibility; остатки и unsupported policy остаются. [[validation/SOURCE_VISIBILITY]].

Read-only visibility residual audit выполнен в Blender:497 residual pixels четырёх прежних native условий,430 neighbor-target texel support/65 source-colored без target-center texel/2 endpoint-bias cases. Local source-visible controls76/69 имеют target support; native RGB reconstruction max0.773484LSB. Independent19/19,reaudit identical. Product renderer не менялся; далее filter-aware visibility/preservation и tolerance/geometry uncertainty. [[validation/VISIBILITY_RESIDUAL]].

Native radial footprint visibility (bilinear_radial_v1, guard0/1) реализована через configure_surface/catalog и проверена на16 frozen captures (§4.75). Outside local198/56→30/6→1/0, но visible hits76/69→12/10→2/2: preservation gate не пройден, default center сохранён. Oracle0 mismatches/2591739 pairs, full build/CTest7/7, carrier10/10 Mesa/NVIDIA, independent20/20, reaudit identical. Далее surface-consistent footprint/preservation и paired tolerance/geometry uncertainty; general mitigation не закрыта. [[validation/FILTER_VISIBILITY]].

§4.76: native bilinear_box_surface_v1 сохраняет central-visible finite-box support,24 matched captures/16 historical hashes exact. Local guard1 outside198/56→1/0, visible75/76 и69/69, IoU0.418994/0.401163; один boundary pixel frame0 не проходит10µm membership (расстояние0.159502mm), strict preservation gate остаётся открытым. Default center сохранён. Oracle0/4319565, CTest7/7, carrier10/10 Mesa/NVIDIA, independent21/21, reaudit identical. Далее paired tolerance/geometry uncertainty; known box не general surface/depth estimator. [[validation/BOX_SURFACE_VISIBILITY]].

§4.77: native support_tolerance_m через configure_surface/catalog;76 frozen sensitivity outputs,8 historical hashes exact, oracle0/15985665, CTest7/7, carrier10/10 Mesa/NVIDIA, independent22/22, reaudit identical. Nominal1mm band/20mm bias сохраняет76/76 и69/69, outside1/0, IoU0.424581/0.401163; при±5mm Z geometry errors robustness не пройдена, широкая полоса может увеличивать false acceptance. Gates23/54 outputs,7/27 paired cells. Default center сохранён; далее uncertainty/unsupported policy и независимый holdout. [[validation/VISIBILITY_SENSITIVITY]].

§4.78: post-hoc exhaustive audit74 local outputs,54 surface outputs имеют600 central/303 remaining-footprint loss memberships на145 unique frame/pixels;18 center controls200 central/0 footprint. Hidden target102/103: off marker hits102/103 не доказывают восстановления скрытых поверхностей. Nominal1mm hits76/69 при IoU0.424581/0.401163 близки к условному visible-only ceiling0.426966/0.401163 (не universal bound). Independent23/23, CTest1/1, reaudit identical; native code не менялся. Проект unsupported/support diagnostics ещё требует native реализации: [[architecture/UNSUPPORTED_SURFACES]], [[validation/VISIBILITY_FAILURE]].
