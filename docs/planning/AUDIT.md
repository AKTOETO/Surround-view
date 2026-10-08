# Аудит TODO и roadmap

Дата: 06.10.2026. Проверены оба файла планирования: корневой `TODO.md` и [[planning/ROADMAP]], а также связанные требования, targets, scripts и тесты. Уточнение клиента от пользователя включено. Это сверка результата с условиями завершения; общий статус диплома не выводится из количества пройденных tests.

## Исправленные расхождения

- Начало roadmap ещё описывало 0.3.0, 22 критерия, шесть нагрузок и отсутствие screening. Сейчас native suite имеет 25 критериев/10 нагрузок, есть 70 first-frame cases; исторические baselines сохранены со своими версиями.
- Qt Bridge дублировал codec и работал напрямую с QLocalSocket. В 0.5.0 выделены `sv-wire`/`sv-client-lib`; GUI и headless consumer используют библиотеку.
- Сервер принимал только обязательный CLI `--ipc-dir`. Добавлены config-driven Unix/TCP listeners, запрет override явного config, TCP-only/combined и отказ UDP до запуска.
- Существующий `step` не имел явного теста; replay игнорировал интервалы manifest. Добавлены step/decode/upload/mesh checks и manifest-driven scheduling. Поворот на паузе сохраняет готовые входы.
- При reconnect на паузе новый клиент мог не получить кадр до следующей команды. Сервер теперь повторяет сохранённый результат для нового data handshake.
- Установленный клиентский API ранее отсутствовал. Теперь внешний CMake consumer использует `sv::client` без Qt/OpenCV/EGL/GLES.

## Проверка каждого этапа roadmap

| Этап | Подтверждённая часть | Незакрытое условие / следующий результат |
|---|---|---|
| M1: постановка/платформа | Тема задана пользователем; главы/источники, Linux паспорт, EGL/Qt проверки | Согласование руководителем, срок/нагрузка, устройство/SDK и приложение на Авроре |
| M2: данные/контракты | Synthetic/Blender manifests, оси/units/calibration, strict JSON рабочего профиля, hashes и assets provenance | Реальная площадка/шаблон, полная нормативная schema, live/source и UDP contracts |
| M3: математика/калибровка | CPU/OpenCV/NumPy projection, synthetic train/validation, detector/fitter и drift/recovery | Полный независимый CPU rasterizer, real-camera metric calibration, resize/crop acceptance |
| M4: одна камера GPU | EGL/FBO/projection/RGBA checks, timer scope и cached inputs | GPU/precision/extensions на устройстве Аврора |
| M5: четыре камеры | Replay/synchronizer, пять carriers, три fusion, coverage/weights, initial screening | Аппаратный backend и интерактивный producer; geometric/seam/ghosting quality truth, graph-cut/multi-band и равный budget |
| M6: диагностика | Сервисный known-XYZ drift/recovery и INDETERMINATE | Overlap detector, реальные false alarm/sensitivity repeats и calibration data |
| M7: сервер/library/UI | Unix/TCP библиотека и GUI, state/control/frame API, release, deadlines/reconnect, installed consumer | Две физические машины; полный latency trace (500-command paused burst проверен); runtime configuration/calibration/health API по capabilities |
| M8: отказы | Bounded sync/framing, replay skew/stale, malformed input, release timeout, reconnect/restart/shutdown | Длительный live overload и аппаратные отказы, distributed clock mapping, долгие memory/thermal runs |
| M9: Аврора | Offline CMake/REQUIRED dependencies, CPU/GPU spec, native suite, Linux package workflow | SDK BuildRequires/ABI, validator/signing/install и весь путь на устройстве; ПК не заменяет эту приёмку |
| M10: основные опыты | Исторические RTX/Mesa серии, calibration/drift и first-frame surface screening | Полные актуальные PC RTX/Mesa baselines готовы ([[validation/CLIENT_SMOKE]]); устройство, независимые validation scenes/real recording, зафиксированные quality thresholds |
| M11: текст/демонстрация | Главы 1–4 и предварительное заключение, подписанные figures/scripts, инструкции | Дополнить результатами всех оставшихся функций; общая редактура после реализации по просьбе пользователя |
| M12: резерв | План предусмотрен | Финальная редакция/защита и воспроизведение после завершения M10/M11; сейчас этап не завершён |

## Сверка всех разделов TODO

| Раздел TODO | Решение аудита |
|---|---|
| Уже полученный результат | Галочки относятся к исторически подтверждённым Linux/synthetic результатам. Старые numbers/versions в measurement reports не обновлять задним числом; новые suites указаны в STATUS |
| Ближайшие действия 1–5 | Теоретический обзор и screening есть, но полное чтение/formula Burger, CPU rasterizer, ground truth/метрики, graph-cut/multi-band и общий budget остаются открытыми |
| Ближайшие действия 6–7 | Чтение автором/согласование руководителем и сбор реальных снимков нельзя отметить выполненными за пользователя |
| Следующий инженерный этап | Library migration, Unix/TCP config и installed consumer закрыты как Linux baseline; составной two-host пункт остаётся открытым. В 0.6.0 replay/socket FrameSource, bounded queues и producer записей реализованы; live hardware и управляемый 3D-стенд ещё не реализованы |
| 1. Постановка/оборудование | Есть паспорт ПК и выбранная тема; срок, доступ к SDK/устройству и согласование — внешние условия. Не заменять их выдуманными данными |
| 2. Минимум чтения | Источники/математические разделы существуют. Чеклист личного чтения автора остаётся личным; полное сравнение методов диагностики и целевые public API требуют отдельного результата |
| 3. Ранние проверки | ПК EGL/FBO/marker/headless/readback/IPC/Qt проверены частично. Составные пункты с устройством и физическим display latency остаются открытыми; нужны единый bandwidth passport и target checks |
| 4. Данные/соглашения | Synthetic/Blender truth и рабочий parser есть. Полный schema, реальные шаблоны, final evaluation split и resize/crop не готовы; составные галочки сохраняются открытыми |
| 5. Первый baseline | CPU/core/calibration/GPU baseline подтверждён. Step/counters/irregular replay теперь проверены. Предварительные quality NFR нельзя уточнить только по smoke timings |
| Уточнение универсальной библиотеки | Каталог текущего API и Unix/TCP policy реализованы. UDP и будущие серверные возможности отдельно остаются открытыми |

## Что можно продолжать без устройства

1. Вынести replay в отдельный `FrameSource`, затем добавить workers OpenCV и producer sockets по camera ID; decode/capture убрать из GL-потока. Проверить bounded queues, stale/skew, reconnect и shutdown каждого источника.
2. Дополнить Blender exporter depth/visibility/semantic truth и metric markers; отделить validation scenes. Реализовать independent CPU rasterization/quality metrics и сравнение при общем resource budget.
3. Зафиксировать photometric policy, реализовать graph-cut и multi-band с измерением памяти, seams/ghosting/temporal stability. Burger добавить только после восстановления точного опубликованного определения.
4. Расширить библиотеку/runtime configurator по серверным capabilities; выполнить command burst, cancellation и overload tests. Для UDP сначала определить datagram semantics, затем писать codec/adapter.
5. Продолжить интерактивный 3D-стенд/producer после FrameSource; scripted offline trajectory не считать управляемым автомобилем.
6. Полные RTX/Mesa baselines 0.5.0 и актуальный RPM smoke сохранены ([[validation/CLIENT_SMOKE]]). Продолжить длительные stress runs. Устройство/SDK и two-host испытание описывать отдельно от loopback.

Незавершённые пункты не удалены и не отмечены выполненными ради чистого чеклиста. Инструкции рабочего клиента — [[engineering/CLIENT_LIBRARY]], границы реализации — [[prototype/STATUS]].

Последующая проверка: CPU/GPU RPM 0.5.0 и отдельные development consumers прошли; полный RTX/Mesa workload comparison сопоставим. 500 orbit-команд на паузе в пакетах по 16 проходят без новых decode/upload/mesh builds. Первичные отчёты и точные границы — [[validation/CLIENT_SMOKE]].

## Дополнение 0.6.0

Устранены blocking decode в GL-потоке и отсутствие virtual-camera endpoints. `FrameSource`/producer recordings, queues/pause/reconnect/fault cases реализованы и проверены: [[validation/SOURCES_SMOKE]]. Аудит 07.10.2026 добавил начальный OpenCV/V4L2 adapter, но физические устройства и shutdown через драйвер не проверены; камера отмечает host delivery clock, не sensor exposure clock. Полные clocks, длительные/quality опыты и M9 SDK/устройство остаются открытыми.

Повторная package qualification 0.6.0 и новые PC RTX/Mesa baselines завершены; оба RPM содержат native source fixture. Strict comparison и происхождение: [[validation/SOURCES_SMOKE]]. Сквозной Blender/socket producer также проверен на сохранённом мире; интерактивное вождение остаётся открытым.

## Host producer recovery

Выполнен автоматический reconnect каждой камеры, late-drop вместо catch-up burst, ограниченные retry/таймауты и сохранение JSON/Markdown report при завершении/ошибке/сигнале. Проверен настоящий server restart для Unix/TCP и изоляция slow receiver; [[validation/PRODUCER_RECOVERY]]. Оставшиеся составные пункты clock mapping, physical hosts и длительного overload не отмечены завершёнными.


## Dense CPU-reference: выполненная часть исследования

06.10.2026 добавлены `tools/reference.py` и `tools/compare_reference.py`: аналитические пересечения plane/bowl/dome-floor/cylinder/cube, независимый fisheye через atan2, билинейная выборка и три fusion-режима. Сохранены 30 offline случаев, coverage/weights/point maps, SHA-256 и иллюстрация. Десять новых CPU проверок и 13 CTest-групп проходят. [[engineering/RENDERING#Независимый аналитический CPU-эталон]], [[validation/ANALYTIC_REFERENCE]].

Это закрывает подготовку плотного геометрического эталона в E-STITCH-01, но не полный CPU rasterizer автомобиля/сцены или quality benchmark. Следующий результат: независимые depth/visibility/metric markers, маска видимого кузова, количественное CPU/GPU сравнение на одинаковом кадре и отдельных validation scenes. Исторические native baselines/RPM относятся к сохранённым ревизиям; host-инструмент не добавляет критерии в native suite. Регистрация новой CTest-группы меняет CMakeLists, поэтому свежую сборку для нового platform baseline следует идентифицировать её собственным fingerprint.


## Приоритетная архитектура 07.10.2026

Зафиксирована целевая модель [[architecture/CLIENT_SERVER_MODEL]]: server-owned config, несколько сессий, per-session view/subscriptions, intermediate products, optional final output и подробные pipeline spans. Шесть PlantUML процессов задают ожидаемые тесты. GUI перенесён в `examples/sv-client`, начат touchscreen layout; `examples/svctl` предоставляет текущие команды через библиотеку без Qt. Installed consumer находится в `tests/fixtures/client-consumer`. На момент этой записи `sv-simulator` был только контрактом; последующая ревизия добавила начальную Qt 6 control GUI, но live world и редактор камер/observations ещё не реализованы.

Config persistence API, multi-client/control-only, subscriptions/canvas и trace — следующий этап, а не возможности текущего сервера. Он по-прежнему имеет одну сессию и legacy final output. Полный чеклист нового этапа находится в корневом TODO; он имеет приоритет над прежними предложениями прямого редактирования server config клиентами. Protobuf и C++20 не добавлены: решение и условия пересмотра описаны в новом контракте.

## Проверка 07.10.2026: ошибки монтажа

Исторический аудит mount-исследования: Blender recipe и мир сохранены, четыре native OpenCV метода, 120 четырёхкамерных trials, независимые numerical validation и actual GLES изображения описаны в [[validation/MOUNT_CALIBRATION]]. Последующие дополнения ниже закрыли image-derived synthetic experiment и начальные server calibration jobs; остаются quality gate, ownership/cancellation, occlusion и GUI world generation. Это первый ограниченный known-intrinsics опыт, не полное исследование всех семейств.

## Дополнение 07.10.2026: image-derived correspondences

`sv-calibrate board-observations` использует OpenCV для поиска chessboard corners в исходных снимках, строит metric vehicle XYZ из измеренных `T_vehicle_from_board`, сохраняет annotated detections и hashes. Команда собирается в Release и CPU profiles. Это делает подготовку observations image-based, но не завершает исследование качества: pose доски требуется измерить заранее, симметричную ориентацию проверить вручную; полный fitting пока остаётся offline. См. [[engineering/USAGE#OpenCV: внешняя калибровка по изображениям]].

## Дополнение 07.10.2026: первый image-derived результат

Обновлено после Blender E-CAL-IMG-01: проведена генерация RGB досок, native detection, split по позам, fit по первым трём и независимый reprojection по четвёртой. Детектор нашёл 16/16 видов и 54/54 угла в каждой held-out картинке; RMSE nominal 12.95–21.48 px, после fit 0.133–0.146 px. Подробный протокол и provenance: [[validation/IMAGE_CALIBRATION]], исследовательская часть: [[research/IMAGE_CALIBRATION]]. Реальные фотографии, survey error, occlusion/quality sweeps и quality-gated server application остаются открытыми.

## Дополнение 09.10.2026: проверка текущего состояния реализации

Сверено с кодом и тестами после добавления simulator IPC discovery и persistence checks. `examples/sv-simulator` — начальная Qt 6 GUI, а не только контракт. Сервер уже имеет `ConfigStore`, отдельный `state_revision` и асинхронные calibration jobs; применение результата сохраняет extrinsics и пересоздаёт renderer. Подтверждение: `config_store_persistence`, `calibration_job`, `simulator_ipc_discovery`, `client_transports` и полный CTest на ревизии 3e4367f. Незакрытыми остаются полноценный ConfigService, multi-session, quality-gated/cancellable calibration и GUI-редактор мира. Aurora SDK, камеры и two-host acceptance данным прогоном не проверялись.
