# Карта документации

> Рабочая редакция от 06.10.2026. Linux-прототип и первая настольная серия реализованы; обзор альтернативных способов fusion/surface начат, его сравнительный опыт ещё не выполнен. Полные требования и перенос на Аврору остаются предметом дальнейшей проверки.

## Порядок чтения

1. [[research/TOPIC|Тема и границы результата]].
2. [[references/README|Источники и маршрут чтения]].
3. [[architecture/SYSTEM|Компоненты и потоки]], [[architecture/MATHEMATICS|Математика]], [[architecture/DECISIONS|Решения и открытые вопросы]].
4. [[requirements/SYSTEM|Обязательные функции и приоритеты]].
5. [[planning/ROADMAP|План реализации]] и [[planning/THESIS|Структура текста диссертации]]; [[diploma/README|подготовленные главы]].
6. [[research/PROJECTION_AND_STITCHING|Обзор слияния четырёх камер и поверхностей]], [[research/CALIBRATION|Калибровка и диагностика]], [[research/EXPERIMENTS|Эксперименты]], [[validation/ACCEPTANCE|Приёмка и паспорт стенда]].
7. [[engineering/SOURCES|Источники кадров и виртуальные камеры]], [[engineering/BUILD|Сборка]], [[engineering/USAGE|работа с кодом]], [[engineering/AURORA|RPM/SDK]], [[engineering/PLATFORM_TEST|переносимые проверки]], [[engineering/SCENE|фотографическая сцена]], [[engineering/BLENDER|объёмная улица и экспорт камер]], [[engineering/ASSETS|ассеты и восстановление мира]].
8. [[prototype/STATUS|Что реализовано]], [[prototype/MEASUREMENTS|исторические результаты]], [[validation/baselines/PC_RTX|native RTX]], [[validation/baselines/PC_MESA|native Mesa]] и [[validation/BLENDER_SMOKE|Blender→server проверка]].

## Структура и ответственность

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
