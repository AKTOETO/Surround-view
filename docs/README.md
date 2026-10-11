# Карта документации

> Сверено 11.10.2026. Linux-прототип, Qt Quick 3D visual driving preview и Blender depth-truth smoke реализованы. Полный сравнительный опыт fusion/surface, интеграция preview как camera producer и перенос/приёмка на Авроре ещё не выполнены.

## Порядок чтения

1. [[research/TOPIC|Тема и границы результата]].
2. [[references/README|Источники и маршрут чтения]].
3. [[architecture/SYSTEM|Компоненты и потоки]], [[architecture/MATHEMATICS|Математика]], [[architecture/DECISIONS|Решения и открытые вопросы]].
4. [[requirements/SYSTEM|Обязательные функции и приоритеты]].
5. [[planning/ROADMAP|План реализации]] и [[planning/THESIS|Структура текста диссертации]]; [[diploma/README|подготовленные главы]].
6. [[research/PROJECTION_AND_STITCHING|Обзор слияния четырёх камер и поверхностей]], [[research/CALIBRATION|Калибровка и диагностика]], [[research/EXPERIMENTS|Эксперименты]], [[validation/ACCEPTANCE|Приёмка и паспорт стенда]].
7. [[engineering/SOURCES|Источники кадров и виртуальные камеры]], [[engineering/BUILD|Сборка]], [[engineering/USAGE|работа с кодом]], [[engineering/PROTOCOL_IMPLEMENTED|реализованный протокол SV01]], [[engineering/AURORA|RPM/SDK]], [[engineering/PLATFORM_TEST|переносимые проверки]], [[engineering/SCENE|фотографическая сцена]], [[engineering/BLENDER|объёмная улица и экспорт камер]], [[engineering/ASSETS|ассеты и восстановление мира]].
8. [[prototype/STATUS|Что реализовано]], [[prototype/MEASUREMENTS|исторические результаты]], [[validation/baselines/PC_RTX|native RTX]], [[validation/baselines/PC_MESA|native Mesa]], [[validation/BLENDER_SMOKE|Blender→server проверка]], [[validation/DEPTH_VISIBILITY_TRUTH|depth truth]], [[validation/E_STITCH_01_V2|пересчитанная exploratory matrix сшивки]], [[validation/OBJECT_STITCH|кодированная мишень и source-camera IDs]] и [[validation/IMAGE_CALIBRATION|image-derived калибровка]] и [[validation/RASTER_CALIBRATION|detector/solver на растровых изображениях]], [[validation/OPTICAL_FAMILY_CALIBRATION|optical families и метрическая ошибка]], [[validation/CALIBRATION_PROVENANCE|проверка split на сервере]], [[validation/CALIBRATION_COVERAGE|периферийная калибровка при равном бюджете]], [[validation/CALIBRATION_CAPTURE_FACTORS|размер и наклон калибровочной доски]], [[validation/INTRINSIC_DIAGNOSTICS|точные углы, detector и model mismatch]], [[validation/CALIBRATION_SENSITIVITY|чувствительность к направленным UV ошибкам]], [[validation/JOINT_CALIBRATION_INFORMATION|полный joint Jacobian и условная неопределённость]].
9. Исследования швов, coded-object screen, source-ID support и подготовленный moving-target capture: [[validation/OBJECT_STITCH]], [[validation/OBJECT_STITCH_MOTION]]; замороженные protocols: [[research/STITCH_TIMING_PROTOCOL]], [[research/STITCH_OBJECT_SUPPORT_PROTOCOL]], [[research/STITCH_MOVING_OBJECT_PROTOCOL]]. Направленное разделение ответа камеры и board poses: [[validation/CALIBRATION_COMPONENT_ATTRIBUTION]]; solver follow-up: [[validation/CALIBRATION_COMPONENT_SOLVER]]; protocols: [[research/CALIBRATION_COMPONENT_ATTRIBUTION_PROTOCOL]], [[research/CALIBRATION_COMPONENT_SOLVER_PROTOCOL]].

## Структура и ответственность

Практический каталог общения сервера и библиотеки: [[engineering/PROTOCOL_SCENARIOS|18 команд и 11 сценариев SV01 с диаграммами]].

| Каталог | Что хранить | Основная заметка |
|---|---|---|
| `research/` | Постановка, fusion/surface методы, калибровка и программа опытов | [[research/TOPIC]], [[research/PROJECTION_AND_STITCHING]] |
| `references/` | Единственный каталог внешних источников и заметки о чтении | [[references/README]] |
| `architecture/` | Компоненты, математические соглашения, диаграммы, ADR | [[architecture/SYSTEM]] |
| `requirements/` | Проверяемые требования и контракты, каждый ID в одном месте | [[requirements/SYSTEM]] |
| `diploma/` | Связный текст глав, их схемы и воспроизводимые иллюстрации | [[diploma/README]] |
| `prototype/` | Команды рабочего профиля, фактические ограничения и первичные измерения | [[prototype/README]] |
| `engineering/` | Единственные подробные инструкции по code/build/use/RPM и native suite | [[engineering/BUILD]] |
| `planning/` | Полный порядок реализации и структура диссертации | [[planning/ROADMAP]] |
| `validation/` | Приёмка/трассировка; `baselines/` — первичные native Markdown-отчёты | [[validation/ACCEPTANCE]] |
| `archive/` | Прежние варианты темы и происхождение материалов | [[archive/README]] |

## Карта требований

- [[requirements/SERVER|Сервер и GPU-ресурсы]].
- [[requirements/CLIENT|Клиент: Qt Quick, управление и свежесть]].
- [[requirements/CONFIGURATOR|Offline-конфигуратор и калибровка]].
- [[requirements/SIMULATOR|Producer, контрольные данные и неисправности]].
- [[requirements/CONFIGURATION|Конфигурация, камеры и численный пример]].
- [[requirements/PROTOCOL|Framing, сообщения, время и буферы]].
- [[requirements/NFR|Метрики, качество и ограничения ресурсов]].
- [[requirements/OPS|Сборка, запуск и воспроизводимость]].

## Правила ведения

1. Из документов в корне проекта находятся только короткий `README.md` и стартовый `TODO.md`; технические подробности находятся в тематических каталогах `docs`.
2. Тема и задачи определяются в `research/TOPIC`; полный план — в `planning/ROADMAP`. `TODO.md` содержит исполняемый список первых шагов, со ссылками на подробности.
3. Нормативный текст требования хранится в одном файле `requirements/`. Остальные заметки ссылаются на ID. Таблицы приёмки задают проверку, а не повторяют требования.
4. Источник получает один ID `Sxx` в `references/`; описания одной работы для нескольких глав объединяются. Перед цитированием проверять URL, версию, авторов, страницы и дату обращения.
5. Архитектурные решения фиксируются в `architecture/DECISIONS` со статусом и условием пересмотра. Предварительные версии API и пороги не выдаются за подтверждённые.
6. Имена файлов — стабильные ASCII-имена; заголовки и текст — на русском. Внутри хранилища использовать полные пути от `docs`, например `[[architecture/SYSTEM|Архитектура]]`. В корневых файлах — обычные относительные Markdown-ссылки.
7. Схемы хранить в ограждённых блоках `plantuml` с `@startuml`/`@enduml`. Контекст, компоненты и последовательность — `architecture/SYSTEM`; жизненный цикл калибровки — `research/CALIBRATION`. Python-скрипты рядом с главами; `diploma/figures/` разделён на theory/design/implementation/experiments/conclusion. Каждая картинка имеет подпись и отмеченный вид данных. Первичные результаты — Markdown-отчёты, большие traces/records — artifacts.
8. Новую заметку создавать для отдельной ответственности; мелкие дополнения добавлять разделом существующей. Архивные заметки имеют явный статус и не определяют текущие сроки или MUST.
9. Все отчёты о работах писать в Markdown. Будущие большие видеоданные и машинные трассы относятся к `data/` и `artifacts/`, вне хранилища документации; их происхождение и интерпретация описываются в Markdown.

## Реализация и аудит плана

Unix/TCP и API библиотеки: [[engineering/CLIENT_LIBRARY]]. Сверка TODO/roadmap, выполненные части и внешние зависимости: [[planning/AUDIT]].

## Рабочие варианты рендера

Контракт поверхностей, fusion, диагностики и команды comparison harness: [[engineering/RENDERING]]. Первое сравнение на Blender-записи: [[validation/SURFACE_SCREENING]]. Оно дополняет исторические baselines, не заменяет их.

## Obsidian

Открыть `docs` через «Open folder as vault», начать с этой заметки. Wikilinks, обратные ссылки, граф и чекбоксы работают внутри хранилища. Для отрисовки `plantuml` требуется совместимый плагин или внешний просмотрщик; установка плагина не является частью этой реорганизации. Без него исходный код диаграмм остаётся читаемым.

Корневой `TODO.md` лежит вне отдельного хранилища `docs`: открывать его в редакторе проекта. Существующие настройки `.obsidian` в корне сохранены; Obsidian может создать отдельные настройки внутри `docs` при его первом открытии. Оба каталога настроек и установленные плагины исключены из Git через `.gitignore`.

Текущая инженерная проверка 0.6.0: [[validation/SOURCES_SMOKE]], [[validation/baselines/PC_RTX_V06]], [[validation/baselines/PC_MESA_V06]], [[validation/PC_COMPARISON_V06]]. Отчёты 0.5.0 и более ранние сохранены как исторические.

Восстановление producer после отказов и runtime reports: [[validation/PRODUCER_RECOVERY]], [[engineering/SOURCES#Восстановление producer и отчёт]].

Аналитический CPU-эталон пяти носителей: [[engineering/RENDERING#Независимый аналитический CPU-эталон]], [[validation/ANALYTIC_REFERENCE]].

Компоненты, устройства и ОС — две PlantUML-диаграммы: [[architecture/DEPLOYMENT]].

Новая модель сервера и трёх клиентов, config transactions, products и timings: [[architecture/CLIENT_SERVER_MODEL]].

Wire-контракт текущей реализации, калибровочные jobs и пробелы протокола: [[engineering/PROTOCOL_IMPLEMENTED]]. Управление автомобилем в ноутбучной 3D-сцене: [sv-simulator README](../examples/sv-simulator/README.md).

Проверка переноса GUI, Qt-free CLI и GTest: [[validation/CLIENT_RESTRUCTURE]].

Ошибки крепления и калибровка: [[research/MOUNT_CALIBRATION]] → [[validation/MOUNT_CALIBRATION]] → [[diploma/04_EXPERIMENTAL_STUDY#4.17 Восстановление положения камер при ошибках монтажа]]. Извлечение наблюдений из изображений: [[research/IMAGE_CALIBRATION]] → [[validation/IMAGE_CALIBRATION]] → [[diploma/04_EXPERIMENTAL_STUDY#4.18 Внешняя калибровка по рендеренным изображениям]]. Восстановление мира и правила — [[engineering/BLENDER]], происхождение файлов — [[engineering/ASSETS]].

Парная динамическая проверка сшивки (10.10.2026): [[validation/PAIRED_STITCH_TEMPORAL]] — один воспроизводимый клип с движущимся rig и matched Blender truth, ограничения и следующие опыты.

Контроль входного разрешения и видимости: [[validation/STITCH_RESOLUTION_VISIBILITY]] — парные 64/256 capture, исправление скрытых camera helpers и ROI ablation.

Convergence 256→512 и три варианта улицы с EV stress: [[validation/STITCH_CONVERGENCE_ROBUSTNESS]]. Условия готовности итоговых выводов: [[planning/ROADMAP#Критерии готовности исследовательского заключения]].

Движущаяся coded-мишень, синхронные Blender camera/truth кадры, 378-case fusion screen и ограничения: [[validation/OBJECT_STITCH_MOTION]]; frozen setup: [[research/STITCH_MOVING_OBJECT_PROTOCOL]].

Native sequential research и textured boundary follow-up: [[validation/SERVER_BOUNDARY]], frozen [[research/SERVER_BOUNDARY_PROTOCOL]], runtime [[validation/RESEARCH_RUNTIME]]. В §4.45 диплома опубликованы positive/negative paired результаты и actual server RGBA; это прежние fixtures, не holdout.

Multilabel server candidate/cost/global-gap controls: [[validation/MULTILABEL_SEAM]], protocol [[research/MULTILABEL_SEAM_PROTOCOL]]. Новые seeded world/mount recipes: [[research/SEAM_GENERALIZATION_PROTOCOL]]; Blender MCP доступен.

Первое сравнение носителей при общем mesh budget: [[validation/CARRIER_BUDGET]], frozen plan: [[research/CARRIER_BUDGET_PROTOCOL]], диплом§4.48.
