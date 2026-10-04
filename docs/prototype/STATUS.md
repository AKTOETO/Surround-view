# Состояние прототипа и границы подтверждения

Редакция 05.10.2026. Это карта фактически реализованного Linux-профиля, а не объявление завершения всех MUST из [[requirements/SYSTEM]]. Рабочий профиль — `linux-prototype-v1`; его JSON намеренно уже полной проектной схемы из [[requirements/CONFIGURATION]]. Совпадение `schema_version: 1` означает версию этого явно именованного профиля, а не взаимозаменяемость всех полей прежнего YAML-примера.

## Реализовано и проверено

| Работа | Свидетельство | Оставшееся ограничение |
|---|---|---|
| Проекция fisheye, V→C, GLM view, плоскость/bowl | Реальный OpenCV; 8000 NumPy и 8192 native analytic/OpenCV points | Нет CPU-растеризатора полного изображения |
| Валидация JSON | Отрицательные тесты единиц, версии, матриц, камер, поверхности и монотонности | Нет полной JSON Schema нормативного профиля, произвольных масок и resize/crop |
| EGL/GLES 3, четыре текстуры, linear-RGB blending и ego-модель | Mesa/RTX; GPU projection, асимметричный RGBA marker, optional valid draw query | Final readback синхронный; таймер зависит от расширения |
| Ограниченные очереди и синхронизация | C++-проверки возраста, skew, дубликатов, очередей | Только replay; аппаратные timestamps и распределённые часы не проверены |
| Калибровка intrinsics/extrinsics по известным 3D-точкам | Независимые train/validation, RMSE и повторное оценивание | Реальная метрическая площадка отсутствует |
| OpenCV detector и image-based intrinsics | `vision_tools`, synthetic projected images; hashes/disjoint train/validation | Held-out board pose fitted; нет real-camera испытаний и внешней привязки |
| OpenCV VideoCapture recorder | Четыре видеофайла → PNG/manifest/timestamps, `vision_tools` | Не встроен в realtime server; host delivery times не sensor timestamps |
| Фотографическая street-demo | CC0 panorama, три actual GLES ракурса в главе 3 | Общий оптический центр, нет реального параллакса/калибровочной истины |
| Сервисная диагностика задней камеры | Заданный поворот, известные точки, INDETERMINATE | Нет анализа признаков перекрытий и статистики реальных ложных тревог |
| Сервер с Asio и отдельным EGL-потоком | Пауза, управление, timeout/reconnect, shutdown | Один клиент и один ожидающий release; decoding в render-потоке |
| Qt Quick desktop-клиент | Qt 6.11.2/5.15.19 offscreen; исправлен teardown Bridge | Не AuroraApp/Silica application; display latency не измерена |
| SV01 framing и две очереди | Однобайтовое/объединённое чтение, malformed, control/data | Это подмножество протокола, не полная приёмка PRO-F-001…011 |
| Исторические серии 0.1.0 и графики | [[prototype/MEASUREMENTS]], `run_experiments.py` | Короткие synthetic опыты; thermal/memory/display latency в этой серии не измерены |
| Native hardware-comparison suite | 22 pass на RTX/Mesa; raw времена, draw/upload/readback, CPU RSS и hashes | Нет target hardware, long-run/VRAM/IPC UI и display latency |
| Offline RPM/spec и installed resources | Оба Linux native RPM, payload core/report/scene/GPU smoke; Qt/GLSL embedded | Aurora ABI/dependencies/validator/signing/installation не проверены |

## Отображение проектной архитектуры на код

| Проектная часть | Текущая реализация |
|---|---|
| `sv-core` | CMake target и `src/core/`; без Qt/Asio |
| `sv-vision` | Настоящий OpenCV: projection/images/board/intrinsics |
| `sv-validation` | CPU criteria, SHA-256, statistics, system passport и report |
| GPU-часть ядра | Выделенный target `sv-render`, `src/render/` |
| `sv-server` | CLI, собственный поток Asio, основной render-поток |
| `sv-bench` | Изолированный рендер, эффективная конфигурация, численная проверка |
| `sv-platform-test` | Native критерии и portable JSON/Markdown для устройства |
| `sv-client` | Desktop Qt/QML с QQuickImageProvider |
| `sv-configurator` | `tools/configurator.py`, Python/NumPy CLI |
| `sv-simulator` | `tools/simulator.py`, offline-генерация PPM/manifest |
| `sv-calibrate`, `sv-capture`, `sv-scene` | OpenCV detector/fitter, recorder и фотографический generator |

QQuickImageProvider заменяет проектный QQuickItem/QSGTexture. Сервер имеет один io_context-runner и одного GL-владельца; общий worker pool отсутствует. Native qualification проверяет все manifest hashes, обычный replay-loader — ID/формат. Эти различия остаются явными. Основная инструкция по коду: [[engineering/BUILD]], [[engineering/USAGE]], [[engineering/AURORA]], [[engineering/PLATFORM_TEST]].

## Следующие обязательные работы

1. Испытать существующий OpenCV detector/fitter на реальных снимках; измерить привязку к автомобилю и независимые XYZ.
2. Полная машинная схема, контроль calibration/manifest-хэшей, систематическое сообщение пути ошибочного поля, report-контракт.
3. `FrameSource`-интерфейс и подготовка/декодирование в отдельном worker pool; TCP producer и аппаратный адаптер по необходимости.
4. Дополнить draw/upload/readback и RSS измерениями IPC/UI/VRAM/thermal; длительная серия и replay clocks с speed/pause anchors.
5. Диагностика по перекрытиям с движением/светом/skew, независимая настройка порогов, чувствительность и ложные тревоги.
6. Дополнительные форматы и маски, UV/weight diagnostic views, quantitative seam/marker error, адаптивная сетка как отдельный опыт.
7. Устройство/SDK Аврора: проверить dependencies/macros, собрать подготовленный RPM и выполнить native suite на железе; затем Aurora UI integration.

Подробный план остаётся в [[planning/ROADMAP]], исполняемый список начала — в корневом `TODO.md`. Текст глав — [[diploma/README]].

## Уточнение следующего этапа 05.10.2026

Запрошены live-источники `/dev/video*`, виртуальные камеры через отдельные сокеты, удалённые клиент/конфигуратор, движущийся автомобиль в полноценном 3D-мире и замкнутое отображение окружения. **В версии 0.2.0 это ещё не реализовано**: сервер читает replay-manifest и использует локальные Unix control/data sockets; `sv-capture` записывает входы отдельно, `sv-scene` преобразует неподвижную панораму. Параметры live/TCP/dome в текущий config добавлять нельзя: строгий parser их отклонит. Последовательность продолжения — [[planning/ROADMAP#Следующий этап: источники, удалённое управление и 3D-окружение]].
