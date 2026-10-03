# Архитектура системы Surround View

## Назначение и статус

Рабочая редакция от 04.10.2026; основа от 19.09.2026. Архитектура начального Linux-стенда по [аудиту](../archive/AUDIT.md); работоспособность на аппаратуре ещё должна быть проверена. Объём определён в [SYSTEM.md](../requirements/SYSTEM.md), календарь — в [ROADMAP.md](../planning/ROADMAP.md), измерения — в [ACCEPTANCE.md](../validation/ACCEPTANCE.md).

## Платформы и границы

Цель — клиент и рабочий конвейер на устройстве с ОС Аврора. Первый проверочный профиль — локальный replay на Linux. Точное размещение сервера на целевом устройстве, доступ к EGL/GPU и зависимости подтверждаются по [[architecture/DECISIONS]]. Работа рендера только на мощном ПК не считается переносом на Аврору.

`sv-client` — единое имя клиента; прежнее `sv-ui` заменено. `sv-configurator` выполняет offline-калибровку на рабочей станции, формирует отчёт и проверенную конфигурацию. Сервер применяет её между запусками. Конфигуратор не входит в покадровый GPU-путь.

## Компоненты и процессы

| Компонент | Ответственность |
|---|---|
| `sv-core` | Библиотека без Qt и транспорта: координаты, калибровка, поверхность, сетки, отображения, GPU-ресурсы и виртуальная камера |
| `sv-server` | Основной host ядра: конфигурация, `FrameSource`, подбор наборов, команды, выходной адаптер, health и trace |
| `sv-bench` | Host того же ядра на локальных данных: математические и performance-опыты без UI |
| `sv-client` | Qt выбранной версии, Qt Quick (QML), QML-модули, проверенные для данного профиля; показ готового изображения, жесты, статусы и собственные события представления |
| `sv-configurator` | Первоначальная и повторная калибровка, проверка качества, экспорт версии параметров; алгоритмы — [[research/CALIBRATION]] |
| `sv-simulator` | Лёгкий producer известного набора, сценарий времени и неисправностей; полный 3D-мир — расширение |



`FrameSource` возвращает нормализованный кадр, владельца буфера и метаданные. `FileFrameSource` и `ProducerFrameSource` обязательны; `HardwareFrameSource` желателен. Выбор источника между запусками сохраняет одно ядро. Горячая замена — отдельная возможность. `sv-bench` изолирует стоимость метода; испытание server → UI остаётся обязательным.

## Подготовка, кадр и команда

При загрузке конфигурации CPU валидирует параметры, строит аналитическую поверхность и выбранную сетку, готовит UV, маски, веса и GPU-ресурсы. Калибровка определяет проекцию и критерий дискретизации; форму задают параметры автомобиля и поверхности. Пересчёт нужен при изменении этих входов, которое в MVP применяется перезапуском.

При новом наборе выполняются подбор кадров, upload входных текстур, обновление масок доступности, рисование bowl, выборка и нормализованное смешивание четырёх текстур в итоговый FBO. Базовый путь — прямое рисование, без обязательного промежуточного атласа. Прямая проекция в shader служит проверке отображений; исследуемый вариант использует подготовленные координаты и интерполяцию. Стоимость и точность вариантов фиксируются раздельно.

При команде orbit/zoom/preset сервер обновляет состояние и его `state_revision`, использует сохранённые входные текстуры, сетку и калибровку, затем создаёт новый выходной кадр. Частота новых видов может отличаться от частоты входа. Новое рисование не означает новое декодирование или upload неизменных входов (NFR-P-004). Дополнительный атлас возможен после измерения пользы и его собственной ошибки.

## Потоки исполнения и владение

| Поток | Работа и ограничения |
|---|---|
| Вход/файлы | Чтение и валидация, ограниченная очередь каждой камеры; не удерживает GPU-контекст |
| Управление | Независимая ограниченная очередь команд, подтверждения и reconnect |
| Render thread | Единственный владелец EGL/OpenGL-контекста ядра и состояния виртуальной камеры; не ждёт сеть, файл или UI бесконечно |
| Выход/trace | Копирование опубликованных CPU-слотов, неблокирующая запись событий; переполнение учитывается |
| UI event loop | QML, жесты, статус; сетевое чтение вынесено из этого потока |
| Qt Quick scene graph | Создание/обновление/освобождение текстуры отображения в потоке рендера Qt Quick [[references/DEVELOPMENT#S47\|S47]] |

Начальные пределы: вход — 3 кадра на камеру, выход — 3 слота, управление — 64 команды, trace — 4096 событий. Для видео удаляется старейший ещё не используемый элемент; счётчик содержит причину. Занятый буфер не перезаписывается. Абсолютные команды можно заменять последней; относительные orbit-delta объединяются с сохранением суммы и связей `command_id`. При невозможности объединения переполненная команда явно отклоняется.

CPU-слот проходит FREE → WRITING → READY → IN_USE → FREE. GPU-буфер освобождается после fence; CPU-слот — после завершения потребителя. Если свободных выходных слотов нет, публикация пропускается с причиной; управление и рендер продолжаются. При reconnect меняется сессия, позднее подтверждение старой сессии не освобождает новый слот. UI не удерживает слот после завершения собственного копирования; отдельная UI-текстура живёт до окончания чтения scene graph.

## Время, набор кадров и состояния

Локальный профиль использует `CLOCK_MONOTONIC` одного Linux-узла. Шкалы разных узлов имеют разные начала и не вычитаются напрямую [[references/MEASUREMENT#S48|S48]]. Для распределённого профиля требуется `clock_domain` и проверенное отображение `t_local = a·t_source + b` с интервалом действия и оценкой погрешности. При отсутствии отображения сквозная задержка помечается недоступной.

Replay сохраняет `scenario_timestamp_ns`, а время выпуска отображает в текущую шкалу исполнения. Пауза замораживает сценарий; сохранённый кадр обозначается PAUSED, а его реальный возраст остаётся в trace. Пошаговый выпуск задаёт новое время исполнения. Это не отключает обнаружение устаревания live-источников.

Подбор начинает с последнего кадра с наименьшим временем среди доступных камер; для остальных выбирается ближайший кадр в пределах окна, при равенстве — более новый. Недоступные и слишком старые входы исключаются; отсутствие полного синхронного набора не блокирует рендер. Каждый результат сохраняет `frame_set_id`, camera sequence IDs, времена, skew и возраст; фактические численные ограничения находятся в NFR и конфигурации.

| Состояние | Условие |
|---|---|
| STARTING | Инициализация и ожидание первого набора до startup timeout |
| READY | Четыре свежих входа, совпадающая калибровка, skew внутри окна |
| DEGRADED | Доступны 1–3 свежих входа либо нарушено окно синхронизации |
| NO_INPUT | Нет пригодного входа после timeout; вывод маски «нет данных», команды продолжают действовать |
| ERROR | Невосстановимая ошибка конфигурации, GPU или ресурса; код и причина, корректное завершение |

Восстановленный источник допускается после handshake, проверки калибровки и первого свежего кадра. READY возвращается только при пригодном полном наборе. `stale` — текущий признак, `dropped` — счётчик событий. В исходном профиле hold-last отключён; истёкший вход имеет нулевой вес. Если расширение включает hold-last, задаётся конечный предел, stale-overlay и отдельный статус каждого использованного входа.

## Выходной кадр и интерфейс

Внутренние GPU-проходы не требуют чтения полного кадра на CPU (SRV-G-002). В исходном выходном адаптере итоговый FBO читается в CPU-буфер, передаётся по локальному сокету и загружается в текстуру UI. Время readback, передачи и upload UI учитывается отдельно и входит в полную задержку. Номер OpenGL-текстуры не является межпроцессным дескриптором.

В QML результат показывается собственным `QQuickItem` с `QSGTexture`; C++-часть доставляет ему неизменяемый кадр и метаданные. `QGuiApplication` и `QQmlApplicationEngine` создают приложение. QML содержит только представление, жесты и модели состояния. Обновление texture/node соблюдает потоки scene graph [[references/DEVELOPMENT#S47|S47]]. Сервер не зависит от Qt; headless подтверждается отдельным запуском, а не составом UI-библиотек.

UI связывает `frame_id`, `state_revision` и команды с кадром, действительно использованным scene graph. Событие `ui_present_submit` фиксируется через `QQuickWindow::frameSwapped` для этого кадра; смысл сигнала — постановка кадра на представление [[references/DEVELOPMENT#S49|S49]]. Это программная конечная точка; физическое отображение требует отдельного внешнего измерения. Повторный swap той же текстуры не считается новым уникальным набором.

## Решения и профиль начального стенда

Действующие решения и вопросы проверки хранятся в [[architecture/DECISIONS]]. Исходный desktop-профиль OpenGL 3.3/EGL и копируемый IPC применяется только при его проверке на выбранной машине. Версия Qt и профиль OpenGL ES для Авроры заранее не фиксируются.

Удалённый producer — отдельный TCP-профиль после расчёта полосы в [PROTOCOL.md](../requirements/PROTOCOL.md). Протокол v1 общий для локального IPC и сетевого расширения. ZeroMQ, UDP/RTP и новые кодеки не являются параллельными обязательствами.

## Проверка архитектуры

[Контекст](SYSTEM.md#контекст), [компоненты](SYSTEM.md#компоненты) и [последовательность](SYSTEM.md#кадр-и-команда) отражают эту модель. Готовность подтверждают независимый запуск процессов, replay без producer, три конфигурации одним бинарным файлом, T-SRV-001 для headless и T-SYS-006 для полного trace. Единственный план этапов — [ROADMAP.md](../planning/ROADMAP.md).


## Связанные источники

[[references/README|Единый каталог литературы и документации]].


## Контекст

```plantuml
@startuml
title Начальный Linux-стенд и целевое устройство Аврора
' Статус: проектная редакция 19.09.2026. Решения: SYSTEM.md, ADR-001/002/003.
left to right direction
actor "Оператор" as Operator
actor "Исследователь" as Researcher
rectangle "Один Linux-узел: local-reference" {
  database "Запись + manifest\nКалибровка и конфигурация" as Data
  component "sv-simulator\nЛёгкий producer + fault injection" as Producer
  component "sv-server\nFrameSource + сведение времени\nsv-core: GPU + виртуальная камера" as Server
  component "sv-client\nQt целевой версии, Qt Quick (QML)\nЖесты, изображение, статусы" as UI
  component "sv-bench\nТо же sv-core без UI" as Bench
  database "Trace и результаты опытов" as Trace
}
rectangle "Целевой этап: ОС Аврора" {
  component "sv-client на устройстве\n+ sv-core / server после проверки API" as Aurora
}
rectangle "Расширения" {
  component "Удалённый producer\nОтдельный сетевой профиль" as Remote
  component "Live-камеры\nАппаратный адаптер" as Hardware
  component "CAN / сенсоры\nСценарий задней передачи" as Vehicle
}
Researcher --> Data : набор, конфигурация, ROI
Researcher --> Producer : сценарий
Data --> Server : прямой replay (MUST)
Data --> Producer : известные входы
Data --> Bench
Producer --> Server : camera_frame, IPC v1
Researcher --> Aurora : развёртывание и измерения
Operator --> UI : orbit / zoom / presets
UI --> Server : команды + command_id
Server --> UI : копируемый кадр + ревизия + health
Server --> Trace
UI --> Trace : receive / present_submit
Bench --> Trace : метрики ядра
Remote ..> Server : TCP после проверки полосы и часов
Hardware ..> Server : FrameSource
Vehicle ..> Server : нормализованное событие
legend bottom
  Начальный профиль: четыре камеры, replay, отдельный сервер и Qt Quick (QML).
  Пунктир: расширения. Полный 3D-мир и динамика автомобиля — COULD.
  Источники приведены отдельным блоком в исходнике; решения — SYSTEM.md.
endlegend
@enduml
```


## Компоненты

```plantuml
@startuml
title Компоненты, границы ядра и выходного адаптера
' Статус: проектная редакция 19.09.2026. Решения: SYSTEM.md, ADR-001/002/003.
package "sv-core: без Qt и транспорта" {
  [Модель камеры и CPU-эталон] as Camera
  [Аналитическая bowl\nРавномерная / исследуемая сетка] as Geometry
  [UV, маски, веса] as Mapping
  [Виртуальная камера] as View
  [EGL / OpenGL\nRender thread и GPU-ресурсы] as GPU
}
package "sv-server" {
  [File / Producer FrameSource] as Input
  [Сведение часов и набор кадров\nОграниченные очереди] as Sync
  [Валидация конфигурации] as Config
  [Control socket\ncommand_id / state_revision] as Control
  [Выходной адаптер\nИтоговый readback + 3 слота + IPC] as Output
  [Health и trace] as Trace
}
package "sv-client: Qt целевой версии, Qt Quick (QML)" {
  [Клиент IPC\nОграниченная очередь] as Client
  [QQuickItem / QSGTexture\nПоказ готового кадра] as Display
  [QML: жесты и пресеты] as Touch
  [QML: состояние и stale-overlay] as Status
  [События UI\nreceive / present_submit] as UITrace
}
package "sv-simulator" {
  [Чтение manifest и кадров] as Data
  [Replay clock и fault injection] as Producer
}
[sv-configurator\nКалибровка и отчёт] as Calibrator
[sv-bench\nЛокальные данные и измерения] as Bench
Calibrator --> Config : проверенная калибровка
Config --> Camera
Config --> Geometry
Camera --> Mapping
Geometry --> Mapping
Mapping --> GPU
Input --> Sync
Sync --> GPU : набор + доступность
Control --> View
View --> GPU
GPU --> Output : итоговый FBO
GPU --> Trace : GPU timings
Sync --> Trace : возраст, skew, drops
Output --> Client : rendered_frame
Client --> Output : frame_release
Client --> Display : байты и IDs
Client --> Status : health / state
Touch --> Control : команды
Display --> UITrace : реально использованный frame_id
UITrace --> Trace
Data --> Producer
Producer --> Input : camera_frame
Bench --> GPU : то же ядро
note bottom of GPU
  Между внутренними GPU-проходами нет обязательного readback.
  Стоимость выходного копирования и upload UI измеряется отдельно.
end note
@enduml
```


## Кадр и команда

```plantuml
@startuml
title Входной набор, изменение ракурса и представление Qt Quick
' Статус: проектная редакция 19.09.2026. Решения: SYSTEM.md, PROTOCOL.md.
participant "Replay / producer" as Source
participant "FrameSource + подбор" as Input
participant "Контроллер камеры" as Controller
participant "sv-core / GPU" as GPU
participant "Выходной адаптер" as Output
participant "Qt Quick (QML)" as UI
participant "Общий trace" as Trace
Source -> Input : camera_frame(session, sequence, clock, calibration)
Input -> Input : framing / валидация / runtime mapping
Input -> Input : ограниченные очереди, возраст и skew
Input -> Trace : receive / frame_set_id / входы
alt Есть свежие входы
  Input -> GPU : набор и маски доступности
  GPU -> GPU : upload только изменившихся входов
else Нет пригодных входов
  Input -> GPU : NO_INPUT, маска «нет данных»
end
Controller -> GPU : состояние и state_revision
GPU -> GPU : готовая сетка + выборка + blending -> FBO
GPU -> Trace : GPU duration / render_complete
GPU -> Output : итоговый FBO
Output -> Output : выходной readback, ограниченный слот
Output -> UI : rendered_frame(frame_id, frame_set_id, revision, token)
UI -> Trace : ui_receive(frame_id)
UI -> Output : frame_release после копирования
UI -> UI : upload результата в QSGTexture / scene graph
UI -> Trace : ui_present_submit(frame_id, revision)
note over UI,Trace
  frameSwapped связывается с реально использованной текстурой.
  Это программная передача на представление, не физический показ пикселя.
end note
group Команда на замороженных входах (PAUSED)
  UI -> Trace : ui_event(command_id)
  UI -> Controller : orbit / zoom / preset(command_id)
  Controller -> Controller : проверить область, обновить revision
  Controller --> UI : camera_state / ACK или причина отказа
  Controller -> GPU : принятое состояние + IDs команд
  GPU -> GPU : reuse геометрии и входных текстур; новый рендер
  GPU -> Output : новый итоговый кадр
  Output -> UI : frame_id + revision, включающая команду
  UI -> Output : frame_release
  UI -> Trace : первое ui_present_submit этой ревизии
end
group Потеря камеры в live-режиме
  Input -> GPU : исключить stale, DEGRADED / NO_INPUT
  GPU -> GPU : перенормировать доступные веса / no-data mask
  Output -> UI : следующий результат + health / stale-overlay
end
@enduml
```
