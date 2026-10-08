# Валидация, паспорт стенда и трассируемость

## Назначение и статус

Рабочая редакция от 04.10.2026; основа от 19.09.2026. Документ определяет будущие проверки и артефакты. Реализация, целевая аппаратура и результаты экспериментов ещё не представлены; все строки матрицы имеют статус **«свидетельство не получено»**. ID проверки обозначает запланированный сценарий, а не существующий автоматический тест. Связанные документы: [требования](../requirements/SYSTEM.md), [исследование](../research/EXPERIMENTS.md), [план](../planning/ROADMAP.md), [архитектура](../architecture/SYSTEM.md).

## Паспорт эталонного стенда

| Поле | Исходное значение / что заполнить перед измерением |
|---|---|
| profile_id, версия, дата фиксации | local-reference-draft; дата и окончательная версия ещё не зафиксированы |
| CPU, RAM, GPU/VRAM | Модель и объёмы не выбраны; заполнить по фактическому стенду |
| ОС, ядро, драйвер, EGL backend | Не проверены; сохранить точные версии, device path и доступные расширения |
| API и сборка | Desktop-кандидат OpenGL 3.3/GLSL 330; целевой API/GLSL проверяется отдельно; C++20/CMake Release; compiler flags, commit и dirty diff в manifest |
| UI и экран | Qt выбранной версии, Qt Quick (QML); фактический patch, display backend, разрешение экрана, частота и vsync ещё не измерены |
| Вход | Локальный replay, 4×1280×720 RGB8/sRGB, 30 FPS; IDs и хэши данных/калибровки обязательны |
| Выход | 1280×720 RGBA8/sRGB; Qt Quick, копируемый IPC; частота рендера 30 Гц в исходном профиле |
| Геометрия | Хэш формы/ROI/масок, метод сетки, число треугольников, допуски и набор ракурсов |
| Очереди и время | 3 кадра/камера, 3 слота выхода, 64 команды; skew 10 мс, max age 100 мс, hold-last=0; monotonic domain одного узла |
| Транспорт | Unix stream v1, разделённые data/control; readback и upload UI учитываются |
| Измерение | Прогрев 60 с; ≥3 прогона по 180 с; отдельный стабильный прогон 600 с |
| Энергорежим | Частоты, governor/power mode, температура в начале/конце, если доступны |
| Пороги | Предложения NFR; финальная версия фиксируется по раннему baseline до итогового сравнения |
| Завершение | Проектный shutdown timeout 2000 мс; проверить T-OPS-009 |

Паспорт сетевого профиля оформляется отдельно: разрешение, формат, полезная полоса, измеренная пропускная способность, clock mapping и uncertainty. Нельзя приписывать локальные метрики распределённой системе. Начальные числа являются целями исследования, не подтверждёнными возможностями неизвестного GPU.

## Проверочные данные и протокол опыта

| Набор | Назначение и независимый ответ |
|---|---|
| D-ANALYTIC | Центральные лучи, маркеры, известные позы и симметрия; ручные аналитические ответы и OpenCV fisheye [[references/VISION#S04|S04]] |
| D-SCENE | Плоские и вертикальные объекты, перекрытия, движение; независимая геометрия/видимость и известная траектория |
| D-CONFIG-1…3 | Три конфигурации автомобиля/камер; один бинарный файл; отдельные точки и ракурсы для настройки и итоговой оценки |
| D-REAL (SHOULD) | Реальная запись с источником, условиями использования, калибровкой, остаточной ошибкой и отдельными наблюдениями |

Manifest содержит версию формата, источник/лицензию, SHA-256 файлов, калибровку, исходные timestamps, runtime anchors, форму/ROI, траекторию команд, seed и fault schedule. Калибровка/manifest считаются проверенными только после независимых наблюдений. Источник и сервер не могут быть единственными взаимными оракулами.

Для каждого запуска сохранить параметры, прогрев, точные длительности, порядок вариантов, число наблюдений и определение percentile. Принять эмпирический квантиль nearest-rank: элемент с индексом ceil(p·N) в упорядоченной выборке (индексация с 1). Вычислять распределения по каждому прогону; межпрогонный разброс публиковать отдельно. Причины исключения образцов фиксировать до анализа. При потере существенных событий trace задержку не восстанавливать выдуманными timestamps.

## События и измерительная граница

Сохраняются capture/scenario/release, send, receive, set_selected, render_submitted, render_complete, readback_complete, publish, ui_receive и ui_present_submit. `render_complete` — host timestamp наблюдения GPU fence, поэтому включает задержку обнаружения завершения; GPU timer query даёт отдельную длительность, не абсолютное время общей шкалы. Проверить эту разницу в протоколе измерения.

Все CPU-события содержат clock_domain; CLOCK_MONOTONIC разных узлов не имеет общего начала [[references/MEASUREMENT#S48|S48]]. Для удалённого времени сохранить преобразование, интервал действия и погрешность, иначе сквозная метрика недоступна. Replay при паузе/step сохраняет исходную шкалу и новые runtime anchors.

UI пишет `ui_present_submit` по frameSwapped, привязанному к реально выбранному scene graph кадру [[references/DEVELOPMENT#S49|S49]]. Это конец программного пути; физическое появление пикселя не измерено. Для команд сохраняются ID и состав ревизии; учитывается первый результат, включающий команду. Для видеовходов отдельно считать emitted/received/accepted sets/rendered/unique presented, repeats и drops. Формулы и численные цели — [NFR](../requirements/NFR.md).

## Связь задач и результатов

| Задача | Компоненты | Проверки / ожидаемый результат |
|---|---|---|
| R1 | Обзор и постановка | Таблица ближайших аналогов и конкретных отличий; решение о формулировке вклада до E-MESH-01 |
| R2 | Калибровка, координаты, CPU-эталон | E-MATH-01, контрольные точки, маски валидности и ошибки |
| R3 | GPU-конвейер четырёх камер | E-SURFACE-01, покрытие, веса и сопоставление формы |
| R4 | Диагностика и конфигуратор; сетка как расширение | E-DRIFT-01/E-RECOVERY-01; E-MESH-01 при включении |
| R5 | Server, Qt Quick, producer, протокол | E-INTERACTIVE-01, E-ROBUST-01, trace команд/отказов |
| R6 | Bench, метрики и эксплуатация | E-PERF-01, manifests, повтор опыта, графики с ограничениями |

## Матрица обязательных требований

Каждый ID ниже ссылается на единственное нормативное описание. Численный критерий и подробный сценарий находятся в соответствующей строке исходного документа. `checks/<test_id>.json` — ожидаемый отчёт о проверке, а не созданный пустой файл. Все артефакты размещаются внутри будущего `artifacts/experiments/<run_id>/`; сейчас свидетельства не получены. SHOULD/COULD принимаются по указанным в требованиях сценариям только при включении расширения в релиз.

| Задача | Требование | Компонент | Проверка | Ожидаемый артефакт |
|---|---|---|---|---|
| R5 | [SYS-F-001](../requirements/SYSTEM.md) | Полный стенд | T-SYS-001 | `checks/T-SYS-001.json` |
| R3, R5 | [SYS-F-002](../requirements/SYSTEM.md) | Полный стенд | T-SYS-002 | `checks/T-SYS-002.json` |
| R2, R4 | [SYS-F-003](../requirements/SYSTEM.md) | Полный стенд | T-SYS-003 | `checks/T-SYS-003.json` |
| R3 | [SYS-F-004](../requirements/SYSTEM.md) | Полный стенд | T-SYS-004 | `checks/T-SYS-004.json` |
| R5 | [SYS-F-005](../requirements/SYSTEM.md) | Полный стенд | T-SYS-005 | `checks/T-SYS-005.json` |
| R6 | [SYS-F-006](../requirements/SYSTEM.md) | Полный стенд | T-SYS-006 | `checks/T-SYS-006.json` |
| R5 | [SYS-F-007](../requirements/SYSTEM.md) | Полный стенд | T-SYS-007 | `checks/T-SYS-007.json` |
| R2 | [CFG-F-001](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-001 | `checks/T-CFG-001.json` |
| R2, R4 | [CFG-F-002](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-002 | `checks/T-CFG-002.json` |
| R2 | [CFG-F-003](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-003 | `checks/T-CFG-003.json` |
| R2 | [CFG-F-004](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-004 | `checks/T-CFG-004.json` |
| R4 | [CFG-F-005](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-005, E-MESH-01 | `checks/T-CFG-005.json` |
| R5 | [CFG-F-006](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-006, E-ROBUST-01 | `checks/T-CFG-006.json` |
| R2 | [CFG-F-007](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-007 | `checks/T-CFG-007.json` |
| R2, R5 | [CFG-F-008](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-008 | `checks/T-CFG-008.json` |
| R5 | [CFG-F-009](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-009 | `checks/T-CFG-009.json` |
| R6 | [CFG-F-010](../requirements/CONFIGURATION.md) | Config / calibration | T-CFG-010 | `checks/T-CFG-010.json` |
| R6 | [NFR-P-001](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-P01 | `checks/T-NFR-P01.json` |
| R6 | [NFR-P-002](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-P02, T-SYS-006 | `checks/T-NFR-P02.json` |
| R6 | [NFR-P-003](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-P03 | `checks/T-NFR-P03.json` |
| R5 | [NFR-P-004](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-P04, T-UI-004 | `checks/T-NFR-P04.json` |
| R5, R6 | [NFR-P-006](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-P06, E-INTERACTIVE-01 | `checks/T-NFR-P06.json` |
| R2 | [NFR-Q-001](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-Q01, E-MATH-01 | `checks/T-NFR-Q01.json` |
| R4 | [NFR-Q-002](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-Q02, E-MESH-01 | `checks/T-NFR-Q02.json` |
| R3 | [NFR-Q-003](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-Q03 | `checks/T-NFR-Q03.json` |
| R5 | [NFR-R-001](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-R01, E-ROBUST-01 | `checks/T-NFR-R01.json` |
| R6 | [NFR-R-002](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-R02 | `checks/T-NFR-R02.json` |
| R5 | [NFR-R-003](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-R03 | `checks/T-NFR-R03.json` |
| R5 | [NFR-R-005](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-R05 | `checks/T-NFR-R05.json` |
| R5 | [NFR-M-001](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-M01 | `checks/T-NFR-M01.json` |
| R6 | [NFR-M-005](../requirements/NFR.md) | Ядро / измеритель / стенд | T-NFR-M05 | `checks/T-NFR-M05.json` |
| R5 | [OPS-F-001](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-001 | `checks/T-OPS-001.json` |
| R5 | [OPS-F-002](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-002 | `checks/T-OPS-002.json` |
| R5 | [OPS-F-003](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-003, T-SRV-001 | `checks/T-OPS-003.json` |
| R5 | [OPS-F-005](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-005 | `checks/T-OPS-005.json` |
| R6 | [OPS-F-006](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-006 | `checks/T-OPS-006.json` |
| R5 | [OPS-F-007](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-007 | `checks/T-OPS-007.json` |
| R5 | [OPS-F-008](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-008 | `checks/T-OPS-008.json` |
| R5 | [OPS-F-009](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-009 | `checks/T-OPS-009.json` |
| R6 | [OPS-F-010](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-010, E-PERF-01 | `checks/T-OPS-010.json` |
| R5, R6 | [OPS-F-011](../requirements/OPS.md) | Запуск / replay / trace | T-OPS-011 | `checks/T-OPS-011.json` |
| R5 | [PRO-F-001](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-001 | `checks/T-PRO-001.json` |
| R6 | [PRO-F-002](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-002 | `checks/T-PRO-002.json` |
| R5 | [PRO-F-003](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-003 | `checks/T-PRO-003.json` |
| R2, R5 | [PRO-F-004](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-004 | `checks/T-PRO-004.json` |
| R5 | [PRO-F-006](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-006 | `checks/T-PRO-006.json` |
| R5, R6 | [PRO-F-007](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-007 | `checks/T-PRO-007.json` |
| R5 | [PRO-F-008](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-008 | `checks/T-PRO-008.json` |
| R5 | [PRO-F-009](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-009 | `checks/T-PRO-009.json` |
| R5 | [PRO-F-010](../requirements/PROTOCOL.md) | Транспорт / сериализация | T-PRO-010 | `checks/T-PRO-010.json` |
| R3 | [SRV-F-001](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-001 | `checks/T-SRV-001.json` |
| R3, R5 | [SRV-F-002](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-002 | `checks/T-SRV-002.json` |
| R2 | [SRV-F-004](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-004 | `checks/T-SRV-004.json` |
| R4 | [SRV-F-005](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-005, E-MESH-01 | `checks/T-SRV-005.json` |
| R2, R3 | [SRV-F-006](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-006, E-MATH-01 | `checks/T-SRV-006.json` |
| R3 | [SRV-F-007](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-007 | `checks/T-SRV-007.json` |
| R5 | [SRV-F-008](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-008 | `checks/T-SRV-008.json` |
| R5 | [SRV-F-009](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-009, T-UI-004 | `checks/T-SRV-009.json` |
| R5, R6 | [SRV-F-010](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-010 | `checks/T-SRV-010.json` |
| R5 | [SRV-F-011](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-011, E-ROBUST-01 | `checks/T-SRV-011.json` |
| R5 | [SRV-F-012](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-012 | `checks/T-SRV-012.json` |
| R3 | [SRV-G-001](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-G01 | `checks/T-SRV-G01.json` |
| R3, R6 | [SRV-G-002](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-G02 | `checks/T-SRV-G02.json` |
| R2 | [SRV-G-003](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-G03, E-MATH-01 | `checks/T-SRV-G03.json` |
| R6 | [SRV-G-004](../requirements/SERVER.md) | sv-core / sv-server | T-SRV-G04 | `checks/T-SRV-G04.json` |
| R5 | [SIM-F-001](../requirements/SIMULATOR.md) | Producer / данные | T-SIM-001 | `checks/T-SIM-001.json` |
| R3, R5 | [SIM-F-003](../requirements/SIMULATOR.md) | Producer / данные | T-SIM-003 | `checks/T-SIM-003.json` |
| R2 | [SIM-F-004](../requirements/SIMULATOR.md) | Producer / данные | T-SIM-004 | `checks/T-SIM-004.json` |
| R5 | [SIM-F-005](../requirements/SIMULATOR.md) | Producer / данные | T-SIM-005 | `checks/T-SIM-005.json` |
| R6 | [SIM-F-008](../requirements/SIMULATOR.md) | Producer / данные | T-SIM-008 | `checks/T-SIM-008.json` |
| R5, R6 | [SIM-F-009](../requirements/SIMULATOR.md) | Producer / данные | T-SIM-009, E-ROBUST-01 | `checks/T-SIM-009.json` |
| R5 | [UI-F-001](../requirements/CLIENT.md) | Qt Quick (QML) | T-UI-001 | `checks/T-UI-001.json` |
| R5 | [UI-F-002](../requirements/CLIENT.md) | Qt Quick (QML) | T-UI-002 | `checks/T-UI-002.json` |
| R5 | [UI-F-003](../requirements/CLIENT.md) | Qt Quick (QML) | T-UI-003 | `checks/T-UI-003.json` |
| R5 | [UI-F-004](../requirements/CLIENT.md) | Qt Quick (QML) | T-UI-004 | `checks/T-UI-004.json` |
| R5 | [UI-F-005](../requirements/CLIENT.md) | Qt Quick (QML) | T-UI-005 | `checks/T-UI-005.json` |
| R6 | [UI-F-006](../requirements/CLIENT.md) | Qt Quick (QML) | T-UI-006, T-SYS-006 | `checks/T-UI-006.json` |
| R5 | [UI-F-007](../requirements/CLIENT.md) | Qt Quick (QML) | T-UI-007 | `checks/T-UI-007.json` |
| R2, R4 | [SYS-F-009](../requirements/SYSTEM.md) | Калибровка | T-SYS-009 | `checks/T-SYS-009.json` |
| R4 | [SYS-F-010](../requirements/SYSTEM.md) | Диагностика | T-SYS-010 | `checks/T-SYS-010.json` |
| R5, R6 | [SYS-F-011](../requirements/SYSTEM.md) | Целевой профиль | T-SYS-011 | `checks/T-SYS-011.json` |
| R2, R4 | [CAL-F-001](../requirements/CONFIGURATOR.md) | Конфигуратор | T-CAL-001 | `checks/T-CAL-001.json` |
| R2, R4 | [CAL-F-002](../requirements/CONFIGURATOR.md) | Конфигуратор | T-CAL-002 | `checks/T-CAL-002.json` |
| R2, R4 | [CAL-F-003](../requirements/CONFIGURATOR.md) | Конфигуратор | T-CAL-003 | `checks/T-CAL-003.json` |
| R2, R4 | [CAL-F-004](../requirements/CONFIGURATOR.md) | Конфигуратор | T-CAL-004 | `checks/T-CAL-004.json` |
| R2, R4 | [CAL-F-005](../requirements/CONFIGURATOR.md) | Конфигуратор | T-CAL-005 | `checks/T-CAL-005.json` |
| R2, R4 | [CAL-F-006](../requirements/CONFIGURATOR.md) | Конфигуратор | T-CAL-006 | `checks/T-CAL-006.json` |
| R4, R5 | [UI-F-009](../requirements/CLIENT.md) | Клиент | T-UI-009 | `checks/T-UI-009.json` |
| R2, R4 | [SIM-F-012](../requirements/SIMULATOR.md) | Данные | T-SIM-012 | `checks/T-SIM-012.json` |
| R2, R4 | [CFG-F-011](../requirements/CONFIGURATION.md) | Конфигурация | T-CFG-011 | `checks/T-CFG-011.json` |
| R4, R5 | [PRO-F-011](../requirements/PROTOCOL.md) | Протокол | T-PRO-011 | `checks/T-PRO-011.json` |
| R5, R6 | [OPS-F-012](../requirements/OPS.md) | Аврора | T-OPS-012 | `checks/T-OPS-012.json` |
| R2, R4, R6 | [NFR-Q-004](../requirements/NFR.md) | Качество | T-NFR-Q04 | `checks/T-NFR-Q04.json` |

## Пакет приёмки и критерий завершения

```text
artifacts/experiments/<run_id>/
  manifest.json          # окружение, команды, данные, clocks, commit
  effective-config.yaml # фактически применённые параметры
  trace.jsonl           # события producer/server/UI
  metrics.csv           # per-run показатели и знаменатели долей
  checks/               # результаты T-* и E-* с наблюдаемыми значениями
  figures/              # heatmaps, кадры и графики сравнения
  report.md             # методика, выводы, ограничения, причины отклонений
```

Приёмка требует реализованного поведения и пригодного свидетельства каждого MUST, основных E-SURFACE-01/E-CALIB-01/E-DRIFT-01/E-RECOVERY-01/E-TARGET-01, интерактивного опыта и воспроизводимого запуска. Наличие файла теста или библиографической ссылки само по себе не подтверждает выполнение. При недостигнутом пороге публикуется фактическое значение и причина; порог нельзя менять задним числом в том же опыте.

E-MESH-01 является SHOULD и принимается при включении расширения. Для основного исследования положительный выигрыш не является условием честного завершения: отрицательный результат допускается при достоверных измерениях и корректных выводах. Отсутствие реальных данных ограничивает применимость выводов; аппаратные и CAN-расширения не заменяют обязательные проверки.


## Связанные источники

[[references/README|Единый каталог литературы и документации]].

## Паспорт Авроры и калибровочных данных

Оформить отдельный `target-aurora` с моделью устройства, ОС/SDK, разрешёнными библиотеками, Qt, API/расширениями, backend EGL, входом/выходом, форматами и измеренными бюджетами. Разместить команды сборки/установки и Markdown-отчёт E-TARGET-01 вместе с паспортом запуска. Один эмулятор не подтверждает GPU-производительность реального устройства.

Для E-CALIB-01/E-DRIFT-01/E-RECOVERY-01 сохранить геометрию шаблона, обучающие и отложенные наблюдения, известные истинные позы, fault schedule, версию диагностического метода, пороги и условия INDETERMINATE. Общее число кадров одного прогона не равно числу независимых испытаний диагностики.
