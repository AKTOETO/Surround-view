# Начало проекта

Состояние на 07.10.2026. Выполненные пункты относятся к Linux-профилю 0.6.0; полный результат на Авроре и реальные камеры остаются открытыми. Начат обзор способов слияния и mesh-носителей; первый screening пяти носителей и трёх fusion-режимов выполнен; качество швов ещё не измерено. E-CAL-MOUNT-01 и первый Blender image-derived study завершены; первичный survey-error sensitivity sweep также выполнен. Добавлены начальные server calibration jobs и локальный V4L2 adapter, но калибровка ещё не является транзакционным quality-gated применением, а физическая camera source не проверена на устройстве. Подробности: [план реализации](docs/planning/ROADMAP.md), [границы прототипа](docs/prototype/STATUS.md), [карта документации](docs/README.md).

## Уже полученный результат

- [x] Подготовить главы 1–4 и предварительное заключение с источниками, формулами, таблицами и воспроизводимыми рисунками. Результат: [текст диплома](docs/diploma/README.md).
- [x] Проверить оборудование Linux и сохранить профиль двух backend: Mesa llvmpipe и RTX 5070 Ti. Результат: [измерения](docs/prototype/MEASUREMENTS.md).
- [x] Создать CMake targets и offline dependency profile с REQUIRED GLM/Boost/OpenCV/OpenSSL, без FetchContent. Результат: [сборка](docs/engineering/BUILD.md).
- [x] Проверить 8000 точек независимым NumPy-расчётом, ненулевую дисторсию, отрицательные конфигурации, framing и синхронизацию.
- [x] Выполнить калибровку по известным синтетическим 3D-точкам на раздельных train/validation; проверить дрейф и повторное оценивание.
- [x] Проверить replay, pause/orbit, release timeout, reconnect и Qt 5/6 offscreen; исправить teardown Bridge. Шесть CTest-групп и пять CPU ASan/UBSan-групп пройдены.
- [x] Сохранить три запуска каждого варианта плоскость/bowl/сетка/разрешение для обоих backend и построить графики из первичных чисел.
- [x] Использовать настоящий OpenCV: projectPoints, detector/fitter и VideoCapture recorder. Image-based и четырёхфайловый capture тесты пройдены.
- [x] Подготовить фотографическую уличную демонстрацию и подписанные рисунки; сохранить Python-скрипты рядом с главами.
- [x] Создать native suite для будущего устройства, сохранить RTX/Mesa Markdown baselines и строгую проверку сопоставимости. Результат: [методика](docs/engineering/PLATFORM_TEST.md).
- [x] Подготовить GPU/CPU RPM spec, Source0 из Git HEAD и проверить оба native Linux payload без checkout. Результат: [SDK/RPM workflow](docs/engineering/AURORA.md).
- [x] Применить Microsoft clang-format с отдельными функциями/ветвями; зафиксировать изменения последовательными коммитами.
- [x] Построить в рендерере сферический купол и круглый пол, закрепить виртуальную камеру внутри оболочки и убрать прозрачные отверстия фона. Сохранить bowl как отдельный тип для сравнительных измерений.
- [x] Начать исследование объединения четырёх камер и surface carriers: описать direct projection, panorama/cubemap pipeline, hard/feather/graph-cut/multiband и plane/bowl/dome/cylinder/cube. Результат: [обзор и план E-STITCH-01](docs/research/PROJECTION_AND_STITCHING.md); первый screening есть в [отчёте](docs/validation/SURFACE_SCREENING.md), полноценный quality benchmark ещё открыт.

- [x] Создать независимый dense CPU-эталон пяти носителей с аналитическими пересечениями, тремя fusion-режимами и сохранением coverage/weights; выполнить 30 случаев на Blender-записи. Кузов, scene depth/visibility и количественное GPU-сопоставление остаются открытыми. [Результат](docs/validation/ANALYTIC_REFERENCE.md).
- [x] Выполнить E-CAL-SURVEY-01: чувствительность внешней калибровки к ошибкам геодезической привязки доски (25 условий, 125 fits, медианные held-out RMSE и ошибки центра камеры). Результат: [отчёт](docs/validation/BOARD_SURVEY_SENSITIVITY.md), [раздел 4.19](docs/diploma/04_EXPERIMENTAL_STUDY.md#419-чувствительность-внешней-калибровки-к-ошибкам-геодезической-привязки-калибровочной-доски-e-cal-survey-01).

Аудит всех разделов TODO и этапов roadmap: [состояние и условия завершения](docs/planning/AUDIT.md). Личные действия автора и аппаратная приёмка не заменяются проверками прототипа.

## Ближайшие действия

1. Полностью разобрать S68–S71: восстановить точную геометрию Burger, записать входы, seam/blend, ограничения и параметры; не выводить метод из одного названия.
2. Зафиксировать общий протокол [E-STITCH-01](docs/research/PROJECTION_AND_STITCHING.md): сцены/ракурсы, ground truth, photometric policy, coverage/seam/ghosting метрики и resource budget; разделить подбор и validation.
3. Аналитический dense CPU-reference для plane/bowl/dome-floor/cylinder/cube реализован и проверен: [описание](docs/engineering/RENDERING.md#независимый-аналитический-cpu-эталон), [результаты](docs/validation/ANALYTIC_REFERENCE.md). Далее подготовить test scene с физически разнесёнными камерами и метрической истиной. Blender MCP проверен 06.10.2026; процедурная улица и разнесённые centers экспортируются через `tools/blender/`. Следующий шаг — depth/visibility truth и метрические markers; текущая запись подтверждает replay, но не завершает оценку качества.
4. Hard best-angle, edge-feather и angular-feather реализованы и проверены. Далее — photometric policy, graph-cut seam и multi-band после выбора маски; на видео проверить temporal jitter.
5. Plane/bowl/dome/cylinder/cube реализованы; первый screening не имеет равного triangle budget и depth truth. Продолжить сравнение после выбора blend-метода; Burger/custom surface добавить после восстановления точной формулы. Сначала dense-reference quality, затем одинаковые triangle/memory budgets.
6. Прочитать [главу 2](docs/diploma/02_REQUIREMENTS_AND_METHODS.md) и [границы прототипа](docs/prototype/STATUS.md); согласовать постановку и критерии с руководителем.
7. Подготовить реальные шаблонные снимки и измерить привязку камер к ТС. Затем продолжить FrameSource, IPC/UI, RPM и Аврора по полному плану.

## Следующий этап по уточнению 06.10.2026

Начать после фиксации версии 0.4.0. Подробная последовательность и условия приёмки: [расширение источников и окружения](docs/planning/ROADMAP.md#следующий-этап-источники-удалённое-управление-и-3d-окружение).

- [x] Вынести replay в `FrameSource` worker; добавить четыре Unix/TCP входа виртуальных камер, выбор source по config, pause barrier, per-camera bounded queues и producer записей. [Описание](docs/engineering/SOURCES.md), [проверки](docs/validation/SOURCES_SMOKE.md).
- [x] Добавить локальный V4L2/OpenCV `FrameSource`, конфиг `source.type=camera` с выбором четырёх `/dev/video*`, RGB-преобразование и ограниченную очередь; server dispatch подключён. Инструкция: [источники кадров](docs/engineering/SOURCES.md).
- [ ] Проверить capture на реальных V4L2-устройствах: negotiated resolution/format, timestamp policy, disconnect/reconnect и возможность гарантированного stop при зависшем `VideoCapture::read`. Текущий `camera_source_lifecycle` проверяет только очередь управления, без камеры.
- [x] Реализовать bounded Unix/TCP передачу RGB8 с calibration/session/sequence/clock metadata; проверить насыщение четырёх очередей, отказ камеры и reconnect с новой сессией.
- [x] Добавить автоматический reconnect host producer, отдельный retry budget каждой камеры, пропуск просроченных кадров и JSON/Markdown отчёт. Проверить перезапуск настоящего сервера на Unix/TCP и отказ медленного приёмника. [Проверка](docs/validation/PRODUCER_RECOVERY.md).
- [ ] Измерить capture/network skew на двух физических машинах, clock mapping и длительную перегрузку/RSS/thermal.
- [x] Разработать Linux Unix/TCP baseline `sv-client-lib` и перевести `sv-client` на её API; общий codec, handshake/команды/кадры, таймауты/reconnect и installed `sv::client`. TCP loopback проверен; отдельное two-host испытание оставлено ниже. Контракт и приёмка: [клиентская библиотека](docs/requirements/CLIENT.md#клиентская-библиотека-sv-client-lib-план).
- [x] Добавить серверную конфигурацию Unix/TCP control/data listeners и общий клиентский API для подключения по IP. Команды/ограничения — [инструкция](docs/engineering/CLIENT_LIBRARY.md). Two-host испытание и удалённое изменение конфигурации ещё открыты; аппаратные камеры будет открывать сервер на своём устройстве.
- [ ] Проверить один API библиотеки на Unix и TCP: одинаковые команды и RGBA-маркеры, разделённые control/data, разрыв/reconnect, ошибки протокола, ограничение очередей и shutdown. Проверить TCP также между двумя машинами и использование установленного CMake-пакета из отдельного приложения.
- [x] Подключить Blender MCP и создать процедурную метрическую улицу с разнесёнными камерами, scripted motion и offline replay export; проверить сервер и сохранить иллюстрации. [Инструкция](docs/engineering/BLENDER.md), [проверка](docs/validation/BLENDER_SMOKE.md). Мир сохранён скриптами и `.blend` в `assets/scenes/metric-street/` без Git LFS; [каталог ассетов](docs/engineering/ASSETS.md).
- [ ] Дополнить Blender-сцену depth/visibility/semantic truth, ground/raised markers и отдельными validation scenes для исследования fusion.
- [ ] Создать отдельный управляемый 3D-стенд: автомобиль движется по сцене, четыре физически разнесённые камеры создают входы сервера. Сохранить независимую истину поз и timestamps.
- [ ] Расширить текущий полусферический купол до исследования полного видимого окружения с настоящим 3D-источником и движущимися физически разнесёнными камерами. Учитывать FOV, маскировку и ненаблюдаемые области явно.
- [x] Добавить native source lifecycle/saturation tests и интеграцию настоящего сервера с четырьмя Unix/TCP producer-входами; существующие render tests проверяют геометрию. [Результаты](docs/validation/SOURCES_SMOKE.md).
- [ ] Расширить испытания на аппаратный backend, интерактивный 3D producer и независимые quality scenes. GTest допустим как системный пакет, без скачивания из SDK.

Ниже сохранён полный стартовый чеклист. Объединённые пункты остаются открытыми, если выполнена только их Linux- или синтетическая часть.

## 1. Зафиксировать постановку и оборудование

- [ ] Прочитать [тему и границы](docs/research/TOPIC.md); согласовать с руководителем цель, вклад, обязательность Авроры, первоначальной калибровки и диагностики смещения. Результат: согласованная редакция постановки.
- [ ] Уточнить срок защиты, недельную нагрузку и доступ к целевому устройству; привязать этапы M1–M12 к календарю с резервом.
- [ ] Заполнить [паспорт стенда](docs/validation/ACCEPTANCE.md): CPU/GPU, память, ОС/SDK, Qt, compiler, display, форматы и каналы ввода.
- [ ] Разобрать [открытые ADR](docs/architecture/DECISIONS.md): где выполняется сервер, доступен ли отдельный EGL-контекст, какие зависимости и графический профиль поддерживаются.

## 2. Прочитать необходимый минимум и связать с опытом

- [ ] First Principles: координаты, intrinsics/extrinsics, проекция; сделать схему преобразований и вручную проверить несколько точек.
- [ ] OpenCV Camera Calibration и Fisheye: разобрать projectPoints/calibrate и остаточную ошибку. Сделать независимый расчёт центрального луча и нескольких точек.
- [ ] TI 3D Surround View и Surround View Calibration: выписать входы, поверхность, offline/online-стадии и ограничения в таблицу аналогов.
- [ ] Документация Авроры по SDK, public API, Qt/QML и профилированию: проверить версии и список доступных зависимостей на конкретном устройстве.
- [ ] OpenGL ES/EGL и Qt scene graph: изучить контекст, текстуры, FBO, precision, владение ресурсами и поток рендера.
- [ ] Zhang и Kannala–Brandt: сформулировать модель калибровки и область её применимости.
- [ ] Работы по extrinsic-калибровке и NIST: выбрать сравниваемый метод диагностики и план независимых повторов.

Ссылки, описания и разделы для чтения: [каталог источников](docs/references/README.md). В записи источника сохранять дату чтения, версию, страницы и полезный вывод.

## 3. Провести ранние технические проверки

- [ ] Собрать минимальное C++/QML-приложение SDK, установить и запустить на устройстве с Авророй. Сохранить команды и версии в Markdown-отчёте.
- [ ] Проверить контекст выбранного API, shader, текстуру и FBO на ПК и устройстве; перечислить реально доступные расширения/форматы.
- [ ] Проверить отдельный GPU-процесс без UI на выбранной платформе; записать условия headless и права доступа.
- [ ] Протестировать асимметричный RGB-маркер: порядок каналов, строки, пропорции и передачу FBO → IPC → QML.
- [ ] Измерить readback, копирование, IPC и upload результата; получить первый бюджет времени и памяти.
- [ ] Посчитать полосу четырёх входов и выхода для выбранного разрешения/FPS; зафиксировать локальный и возможный сетевой профили отдельно.

Если критическая проверка не проходит, обновить ADR и профиль перед полноценной GPU-реализацией.

## 4. Подготовить данные и математические соглашения

- [ ] Прочитать [математическую модель](docs/architecture/MATHEMATICS.md); проверить оси, единицы, V → C, порядок матриц и виртуальный view.
- [x] Создать небольшой независимый синтетический набор четырёх камер: маркеры, известные intrinsics/extrinsics, исходные timestamps, истина и хэши. Результат: `tools/simulator.py` и [воспроизводимая серия](docs/prototype/MEASUREMENTS.md).
- [ ] Подготовить шаблон и наблюдения для первоначальной калибровки; определить привязку камер к общей системе автомобиля.
- [ ] Разделить наборы настройки/калибровки и итоговой оценки; заранее определить ROI и траекторию виртуальных ракурсов.
- [ ] Реализовать структурную схему и parser по [конфигурации](docs/requirements/CONFIGURATION.md); пример из документа остаётся синтетическим fixture.
- [ ] Проверить центральный луч, известные повороты, FOV, NaN/Inf, resize/crop, ориентацию строк и ошибочные конфигурации.

## 5. Создать первый измеряемый baseline

- [x] Создать CMake-каркас `sv-core`, независимых математических проверок и `sv-bench`; подтвердить сборку без Qt в ядре. Проверена также CPU-сборка без EGL/Qt.
- [x] Реализовать CPU-проекцию и сравнить с аналитическими ответами и независимой NumPy-моделью уравнений OpenCV. Результат: `tests/test_math.py`, [раздел 4.2](docs/diploma/04_EXPERIMENTAL_STUDY.md#42-проверка-математического-ядра).
- [x] Выполнить known-XYZ калибровку и OpenCV image-based fitting на раздельных synthetic train/validation. Проверка детектора на реальных шаблонах и метрическая привязка остаются открытыми.
- [x] Реализовать одну камеру на плоскости/bowl с orbit/zoom, затем четыре камеры с валидностью и нормализованными весами. Linux baseline подтверждён численными и интеграционными проверками.
- [x] Проверить step, decode/upload/mesh counters и нерегулярные scenario-time интервалы: `tests/test_client_transport.py`. Исправить фиксированные 30 FPS на интервалы manifest и повторную выдачу кадра после reconnect на паузе.
- [ ] Использовать сохранённые coverage/ошибки/GPU-timings и уточнить предварительные [пороги NFR](docs/requirements/NFR.md) до основных сравнений.

После этих результатов перейти к M6–M12 в [подробном плане](docs/planning/ROADMAP.md): диагностика смещения, сервер/клиент, отказы, целевой перенос и основные эксперименты.

## Уточнение универсальной sv-client-lib

- [x] Описать каталог текущих операций библиотеки, capabilities, состояния/события, ACK/error, таймауты и примеры: [рабочий API](docs/engineering/CLIENT_LIBRARY.md). Будущие server configuration/calibration/source-management операции добавляются после реализации сервера. [Контракт](docs/requirements/CLIENT.md#клиентская-библиотека-sv-client-lib-план).
- [x] Добавить серверную конфигурацию Unix/TCP listeners и явный отказ неподдержанного UDP. Проверить Unix-only без IP sockets через `/proc` (IPv4/IPv6 TCP/UDP), TCP-only без Unix paths и combined profile. UDP-реализация остаётся отдельным пунктом ниже. [Политика подключения](docs/requirements/CONFIGURATION.md#разрешённые-подключения-сервера-план).
- [ ] После Unix/TCP спроектировать UDP-профиль: назначения каналов, packetization/reassembly, sessions, loss/reorder/duplicates, deadlines и подтверждение команд; затем реализовать адаптер и тесты. Текущий stream-протокол сам по себе UDP не обеспечивает.

Проверено после аудита: 500 команд на паузе, полные RTX/Mesa baselines 0.6.0 и оба Linux RPM 0.6.0 с development consumer. [Результаты 0.6.0](docs/validation/SOURCES_SMOKE.md), [предыдущая серия](docs/validation/CLIENT_SMOKE.md). Replay/socket FrameSource имеет baseline; начальный V4L2 adapter добавлен, но физические камеры ещё не проверены. Открыты live Blender render, UDP/quality и целевой перенос.

## Перестройка по требованиям 07.10.2026: сервер и три клиента

Приоритетный контракт: [ответственность, шесть PlantUML workflows и проверки](docs/architecture/CLIENT_SERVER_MODEL.md). Этот этап заменяет прежние предложения о прямой записи server config конфигуратором. Существующие offline файлы на ноутбуке остаются черновиками/fixtures, не способом изменения работающего сервера.

## Aurora RPM dependencies

- [ ] Подготовить отдельные target RPM для GLM и OpenCV (development/runtime части, версии и ABI под выбранный SDK), опубликовать их в репозитории SDK и проверить `mb2 installdeps`.
- [ ] Разрешить build-host CMake ≥3.20 и Ninja через SDK build environment; CMake не является dependency, которую проект может получить через FetchContent до запуска сборки.
- [ ] Собрать GPU и CPU application RPM с FetchContent выключенным, проверить RPM Requires/Provides, payload и запуск на target. До появления SDK/устройства этот пункт остаётся непроверенным.
- [x] Сохранить FetchContent только как system-first fallback для developer CMake builds; Aurora spec использует отдельные GLM/OpenCV BuildRequires и сеть во время упаковки не требует.

### Границы и примеры

- [x] Зафиксировать ответственность: сервер вычисляет продукты и единолично валидирует/применяет/сохраняет config; библиотека предоставляет API; приложения — её потребители.
- [x] Описать целевые процессы: connect/multi-session, config transaction, подписки/отключение final, trace, live simulator; привязать к unit/integration сценариям. Это архитектурный контракт, не отметка готовой реализации.
- [x] Перенести существующий GUI в `examples/sv-client/`; installed consumer — в `tests/fixtures/client-consumer/`; сохранить binary/API и offline сборку.
- [x] Создать `examples/svctl/`: Boost/STL без Qt, существующие команды через `sv-client-lib`, JSON ACK, deadline/exit codes, отсутствие прямой записи config.
- [x] Начать touchscreen UI: крупные кнопки ракурсов/масштаба, single-touch orbit и панель state; убрать инженерные replay-команды с главного экрана.
- [ ] Завершить Qt 5 automotive client под Аврору: платформенный lifecycle/UI integration, DPI и физические touch targets, жесты, информационная вкладка, проверка реального экрана.
- [x] Создать начальную оболочку Qt laptop GUI `examples/sv-simulator/`: сборка, Unix/TCP connection, базовые команды и отображение кадров.
- [x] Добавить в laptop GUI редактируемые Unix/TCP endpoints, timeout/reconnect options, сохранение настроек и ограниченный поиск локальных IPC каталогов по `SV_IPC_DIR`, `$XDG_RUNTIME_DIR` и документированным defaults; показывать источник, камеры и доступные pipeline timings.
- [ ] Добавить типизированные серверные API чтения/изменения конфигурации, прежде чем показывать переключатели fusion, source, mesh или output в GUI. Сейчас доступны только команды ракурса, replay control, calibration job status/apply и чтение frame metadata.
- [ ] Довести `sv-simulator` до инженерного инструмента: world/camera editing, управляемое движение, valid calibration observations и полноценные эксперименты. Текущая кнопка калибровки отключена, пока GUI не собирает реальные observations.

### Сервер, конфигурация и библиотека

- [x] Разбить `server.cpp` на sessions/transport, config, pipeline, products и telemetry; реализованы `ConfigStore` (`include/sv/config_store.hpp`) и `SessionRegistry` (`include/sv/server_session.hpp`).
- [ ] Реализовать несколько одновременных клиентов: сейчас `SessionRegistry` — вспомогательный каркас, но серверный accept/render path остаётся односессионным.
- [x] Добавить control-only client mode: CLI `svctl` подключается к control channel и отправляет команды без открытия видеоканала.
- [ ] Сделать view/subscriptions локальными для сессии; сохранить глобальные source/calibration/fusion/default-view только через ConfigService.
- [ ] Реализовать типизированные client API для config read/update/validate/status и подписок/trace; wire codec скрыть за доменным API.
- [x] Добавить базовое серверное хранение конфигурации и атомарную запись JSON при применении calibration; renderer пересоздаётся из обновлённой конфигурации.
- [ ] Завершить транзакционный ConfigStore: immutable snapshots, optimistic concurrency, frame-boundary apply, pending_restart и устойчивость к ошибкам/повторным операциям.
- [ ] Обработать duplicate operation IDs, lost ACK/status query, disk errors и восстановление после падения; библиотека не повторяет мутации вслепую.
- [x] Добавлять каждую новую серверную операцию одновременно в `svctl`; CLI покрывает операции управления, подстроек и асинхронной калибровки (`calibration-status`, `apply-calibration`).
- [ ] Перевести offline configurator на черновики/экспорт и server API для применения; никакой клиент не изменяет серверный файл напрямую.
- [x] Оценить Protobuf: на первом этапе оставить SV01/Boost.JSON, зафиксировать причины и условия пересмотра; не добавлять protoc/runtime без обоснованной потребности и SDK-проверки.

### Управление продуктами и измерения pipeline

- [ ] Реализовать per-session subscriptions с products/camera mask/view/resolution/FPS, negotiated capabilities и effective revision boundary.
- [ ] Отключать выдачу final frame отдельно для каждого клиента; при отсутствии всех final consumers пропускать final render/readback/encode.
- [x] Добавить самостоятельный `stitched_canvas` до virtual view: прямоугольный холст, явная projection/domain/validity, выбор любых камер, в том числе трёх из четырёх; нормировка весов пересчитывается автоматически (проверено в `sv-render-mode-tests`).
- [ ] Публиковать промежуточные camera frames, coverage, четыре веса, projection maps и metadata через общий product API; ограничить память/полосу/частоту.
- [x] Добавить ограниченный ring buffer и фактически доступные server spans: source poll, pre-render preparation, render wall, optional GPU draw query, upload/readback CPU intervals, publish enqueue и delivery-to-render duration (`include/sv/pipeline_spans.hpp`).
- [ ] Добавить отдельные инструментированные receive/decode, queue wait, synchronization, projection/fusion, network send и client receive/present spans; для неисполненных стадий передавать отсутствие значения, не нулевое время.
- [ ] Отделять CPU wall, GPU query validity и client receive/present; не суммировать перекрытия и не вычитать часы разных машин без clock mapping.
- [ ] Проверить slow subscriber isolation, no-final counters, три-camera masks, stale revisions, budget_exceeded и trace saturation.

### Один инженерный инструмент и полноценный мир

- [ ] Включить в laptop GUI все существующие пользовательские режимы: synthetic/photographic/Blender datasets, replay/producer/capture, calibration, carrier/fusion, reference/experiments/reports и assets provenance.
- [ ] Выделить reusable application services и job adapters с progress/cancellation/error/report; не вставлять shell commands в QML и не создавать новые разрозненные launch scripts.
- [ ] Реализовать загрузку карты/сцены и автомобиля, управление движением, camera rig, четыре live streams, независимые pose/timestamp truth, deterministic pause/reset.
- [ ] Использовать сервер на целевом устройстве и simulator на ноутбуке: control/output через `sv-client-lib`, изображения — отдельным producer protocol; провести настоящий two-host опыт.
- [ ] Переносить функции Python tools поэтапно с проверкой эквивалентности; сохранять scripts восстановления мира, независимые oracles, тесты и рисунки диплома.

### Поддерживаемость и приёмка

- [x] Ввести небольшие интерфейсы в местах внешних эффектов (`IConfigStore`, `IClock`, `IFrameSource`, `IProductSink`), RAII/PImpl и `MockClock`/`MockProductSink` реализации; добавлено в `include/sv/interfaces.hpp` и протестировано.
- [x] Начать C++ unit tests на системном GTest для CLI; без FetchContent. Python оставить для integration и независимой математики.
- [x] Сделать Release C++ checks активными (не `assert` под `NDEBUG`); добавить GTest на atomic ConfigStore persistence/rejection и фильтрацию IPC discovery candidates. Calibration job проверяет persisted extrinsics, а transport integration запускает GUI simulator smoke для TCP и auto-discovered Unix.
- [ ] Добавить дополнительные GTest/mock сценарии полноценной config transaction, server sessions, scheduler/subscriptions и timing clock; аппаратную camera capture проверять только на доступном V4L2 fixture/device.
- [x] Оставить C++17 до конкретной необходимости C++20 и проверки Aurora toolchain; не повышать стандарт только ради номера.
- [ ] Обновить главы диплома/архитектуру/RPM/USAGE после каждого реализованного этапа; финально привести документацию к единому актуальному описанию без устаревших утверждений.

## Исследование калибровки при ошибках крепления камер — 07.10.2026

- [x] Создать отдельную Blender-улицу с автомобилем и четырьмя центрами камер, seed/rules и независимыми номинальными/фактическими позами. Сохранить рецепт и `.blend` в assets без LFS.
- [x] Добавить задаваемые yaw/pitch и сдвиг вдоль кузова: random bounds и per-camera overrides. Проверить воспроизводимость, rigid transforms и оси.
- [x] Начать E-CAL-MOUNT-01: известные intrinsics, независимые train/validation XYZ, контролируемые шум/выбросы; сравнить ITERATIVE, EPnP, SQPnP и RANSAC+EPnP+LM на настоящем C++ OpenCV, сохранить численные результаты и actual GLES изображения.
- [x] Реализовать image-derived соответствия: native OpenCV chessboard detector для снимков четырёх камер, измеренная поза доски в координатах ТС, ручной контроль неоднозначного порядка углов, annotated outputs и provenance. Подробности: [[engineering/USAGE#OpenCV: внешняя калибровка по изображениям]].
- [x] Добавить начальный асинхронный `CalibrationJobManager` и команды `calibrate`, `calibration_status`, `apply_calibration`.
- [ ] Завершить безопасное применение калибровки: защита от конкурентного apply. Session ownership, cancel command и отмена jobs при отключении control-клиента добавлены; OpenCV-вызов отменяется кооперативно по завершении текущего solver. Renderer строится до persistence/swap, поэтому ошибка не меняет активные config/renderer. Held-out gate имеет предварительные пределы 3 px RMSE / 8 px max, которые нужно подобрать на физических данных и оформить версионированной policy.
- [ ] Сделать обзор семейств калибровки с источниками и матрицей применимости: intrinsics (pinhole/Brown, fisheye/Kannala–Brandt и альтернативные omnidirectional models), extrinsics (PnP/IPPE/SQPnP, robust estimation/refinement), joint/multi-camera calibration и overlap/photometric approaches. Не объявлять четыре PnP варианта сравнением всех существующих механизмов.
- [x] Провести первый image-derived proof E-CAL-IMG-01: Blender RGB шахматной доски → native OpenCV corners → metric correspondences → fit на трёх позах и отдельная held-out проверка на четвёртой; 16/16 видов обнаружены. Результаты, рисунок, hashes и ограничения: [[validation/IMAGE_CALIBRATION]], исследовательское описание: [[research/IMAGE_CALIBRATION]]. Это только синтетический начальный опыт, не проверка реальных камер.
- [ ] Расширить image-based исследование: coded/asymmetric targets (ChArUco/AprilTag-подобные), ground/raised и planar/nonplanar точки, board-pose survey error, качество detector, pose diversity, occlusion/visibility, blur/glare и несколько независимых validation scenes. Затем повторить на физически измеренных снимках; не считать E-CAL-IMG-01 достаточной приёмкой точности.
- [ ] Исследовать совместное восстановление intrinsics/extrinsics, ошибку модели дисторсии, число/размещение контрольных точек, разнообразие поз шаблона и чувствительность к начальному приближению. Сопоставлять методы с одинаковыми входными предпосылками.
- [ ] Расширить mount sweep по амплитуде/распределению ошибок, seeds, illumination, skew, blur и occlusion; разделить подбор RANSAC/robust параметров и итоговую оценку. Добавить интервалы неопределённости и вероятность отказа, а не только медианы успешных запусков.
- [ ] Добавить в simulator GUI редактор правил мира/ошибок крепления и запуск calibration jobs/experiments через общие сервисы. Сохранять true poses только в evaluator ground truth, а не выдавать их solver как начальную калибровку.
- [x] Дополнить главу 4 первым опытом E-CAL-MOUNT-01: таблицы точности/отказов, три изображения с подписями, формулы, интерпретация и ограничения. Следующие серии дополняют этот результат; изображения сопровождают независимые метрики.
