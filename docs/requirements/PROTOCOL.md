# Требования к протоколу и данным

## Назначение и статус

Рабочая редакция от 04.10.2026; основа от 19.09.2026. Проектный контракт v1 для локальных копируемых кадров и независимого управления; реализация и машинная схема ещё должны быть проверены. Решения: [[architecture/DECISIONS|ADR-002/003]], [конфигурация](CONFIGURATION.md), [приёмка](../validation/ACCEPTANCE.md). CLOCK_MONOTONIC локален узлу [[references/MEASUREMENT#S48|S48]]; оптимизация DMA-BUF имеет отдельный контракт ресурсов [[references/DEVELOPMENT#S50|S50]].

## Требования

| ID | Приоритет | Условие и наблюдаемое поведение | Приёмка | Задача |
|---|---|---|---|---|
| PRO-F-001 | MUST | Сообщение имеет проверяемый framing, версию, тип, session/source/sequence и семантику времени. | T-PRO-001: неполный header/payload, объединённые сообщения и неверные длины. | R5 |
| PRO-F-002 | MUST | Runtime timestamps монотонны внутри сессии и clock domain; удалённая шкала преобразуется явно. | T-PRO-002: нарушение порядка, неизвестный domain, reconnect и replay mapping. | R6 |
| PRO-F-003 | MUST | Handshake допускает только v1 и поддержанные capabilities; несовместимость отвергается до обработки кадров. | T-PRO-003: неизвестная версия/формат/калибровка с reason code. | R5 |
| PRO-F-004 | MUST | Camera frame описывает изображение, калибровку, источник и времена однозначно. | T-PRO-004: stride/size/format validation, отрицательные случаи и восстановление RGB8. | R2, R5 |
| PRO-F-005 | SHOULD | При CAN-сценарии поддерживается Classical CAN и документированное описание сигналов. | T-PRO-005: DLC 0…8, ID 11/29 бит, декодирование reverse signal по fixture. | R5 |
| PRO-F-006 | MUST | UI-команда имеет command_id, время, параметры, ACK/отказ и связь с принятой ревизией. | T-PRO-006: orbit-delta, coalescing, дубликат, ошибочный preset и очередь управления. | R5 |
| PRO-F-007 | MUST | Rendered frame сохраняет frame_id/frame_set_id, входы, state_revision, связанные command_id и буферный token. | T-PRO-007: UI определяет фактическую свежесть и освобождает кадр правильной сессии. | R5, R6 |
| PRO-F-008 | MUST | Malformed, duplicate, out_of_order и drops имеют разные счётчики; stale — состояние входа и отдельная причина исключения. | T-PRO-008: инъекции и различимые события. | R5 |
| PRO-F-009 | MUST | Длины, dimensions, stride, плоскости, очереди и времена ожидания ограничены до выделения памяти. | T-PRO-009: overflow, превышение лимита, truncated stream, timeout и очистка ресурсов. | R5 |
| PRO-F-010 | MUST | Управление и видеоданные имеют независимые ограниченные очереди и соединения. | T-PRO-010: переполнение видео не блокирует команды; trace потерь. | R5 |
| PRO-F-011 | MUST | Telemetry/camera state передаёт calibration_id, диагностическое состояние, метод/показатель, камеры-кандидаты и причины INDETERMINATE. | T-PRO-011: сериализация всех состояний и unknown method; диагностика не подменяет READY/DEGRADED. | R4, R5 |

## Framing, handshake и пределы

Начальный транспорт — Unix domain `SOCK_STREAM`: один control socket и один data socket в `ipc-dir`. Удалённый producer использует два TCP-соединения по той же схеме только в отдельном сетевом профиле. Внешний клиент связывает data с control через идентификатор сессии и одноразовый token handshake.

Каждое сообщение начинается с фиксированного 24-байтового префикса; многобайтовые целые — unsigned big-endian, без padding:

| Поле | Размер | Смысл |
|---|---:|---|
| magic | 4 байта | ASCII `SV01` |
| protocol_version | 2 байта | 1 |
| message_type | 2 байта | 1=hello, 2=hello_ack, 3=error, 10=camera_frame, 11=rendered_frame, 20=ui_camera_command, 21=camera_state, 22=frame_release, 30=telemetry, 40=vehicle_state (расширение), 41=can_frame (расширение) |
| header_size | 4 байта | Весь header, включая 24 байта префикса |
| payload_size | 8 байт | Число байтов binary payload |
| flags | 4 байта | В v1 равно 0; неизвестные флаги отклоняются |

За префиксом идёт JSON UTF-8 длиной `header_size−24`, затем payload. Timestamps и sequence uint64 в JSON кодируются десятичными строками, чтобы JavaScript/QML не теряли точность; state_revision также строка uint64. JSON-числа для геометрии должны быть конечными. Размер header ≤64 КиБ, payload ≤64 МиБ, ширина ≤4096, высота ≤2160, stride ≤65536 байт. Все произведения/суммы размеров проверяются с защитой от переполнения.

Чтение stream накапливает сначала префикс, затем metadata, затем payload; один `read` не обязан совпадать с сообщением. Неполный пакет к дедлайну отклоняется, соединение закрывается с освобождением временного буфера. Начальные пределы: handshake 2 с, сборка сообщения 2 с, 3 входных кадра на камеру, 3 выходных слота, 64 команды и 4096 trace-событий. Более строгая политика свежести применяется независимо от дедлайна чтения.

Hello содержит role, session UUID, source_id, clock_domain, formats, schema_version и calibration IDs. Hello_ack фиксирует принятый профиль и лимиты. До ACK данные запрещены. Новая сессия начинает sequence с 0; wrap uint64 требует новой сессии. Переиспользованный номер в прежней сессии — duplicate, скачок вперёд — счётчик пропущенных номеров; метаданные session/clock проверяются для каждого пакета.

## Метаданные payload

Общие JSON-поля: `session_id`, `source_id`, `sequence_id`, `clock_domain`, `event_timestamp_ns`. Смысл времени определяется типом события, а не подразумевается одним полем timestamp.

| Сообщение | Обязательные дополнительные поля |
|---|---|
| camera_frame | camera_id 0…3, calibration_id/hash, width/height, pixel_format=RGB8, color_space=sRGB, transfer=sRGB, row_origin=top_left, stride_bytes, planes=[offset,size,stride], capture_timestamp_ns (для live), scenario_timestamp_ns и release_timestamp_ns (для replay), send_timestamp_ns |
| ui_camera_command | command_id, type=orbit/zoom/preset, параметры с единицами, ui_event_timestamp_ns; pan только при capability |
| camera_state | принятая/отклонённая команда, reason, state_revision, итоговое состояние, applied_command_ids; при coalescing сохраняется состав команд |
| rendered_frame | frame_id, frame_set_id, state_revision, applied_command_ids, четыре записи inputs (used/missing, session, sequence, calibration, input time, clock domain), width/height, RGBA8/sRGB, stride/planes, row_origin=top_left, health, age/skew, render_complete/readback_complete/publish timestamps, buffer_token |
| frame_release | session_id, frame_id, buffer_token после копирования/завершения использования CPU-данных UI |
| telemetry | тип события, IDs корреляции, clock_domain/time, GPU duration либо CPU duration, счётчики/причины потерь и состояния |

V1 поддерживает одну плоскость interleaved RGB8 или RGBA8; stride ≥ width·bytes_per_pixel, offset=0, payload_size=stride·height. Поля плоскостей резервируют однозначное расширение; NV12/YUV пока не объявляются поддержанными. BGR без явного преобразования не принимается. Кадр с чужой калибровкой отклоняется до рендера.

## Жизненный цикл и время

Выход передаётся как копируемые байты, token относится к ограниченному серверному слоту. Слот не переиспользуется, пока идёт запись/чтение либо нет frame_release. Дедлайн release исходно 250 мс: при превышении сервер закрывает сессию клиента, завершает операции с ней и возвращает только принадлежащие ей слоты. Позднее освобождение старой сессии игнорируется. При отсутствии свободного слота публикация пропускается с причиной. Число GL-текстуры не передаётся как IPC-handle.

Replay отображает исходное время `s` в runtime: `t_release = t_anchor + (s−s_anchor)/speed`, speed > 0. Пауза сохраняет s; после resume/step новые anchor сохраняются в trace. При step release задаётся фактическим текущим временем, исходный s не меняет своего смысла. Для локального опыта UI/server/producer используют один domain; удалённый требует `a,b,uncertainty_ns,valid_interval` преобразования часов. Если погрешность сравнима с заявленной задержкой/окном, результат помечается неопределённым.

Будущий DMA-BUF-адаптер дополнительно описывает FD, DRM format, размеры/плоскости, offsets/strides/modifiers, acquire/release synchronization и владельца. Импорт через EGLImage [[references/DEVELOPMENT#S50|S50]] не отменяет необходимость этого контракта.

## Бюджет полосы

Полезная нагрузка четырёх потоков: `4·width·height·bytes_per_pixel·fps`; МБ/с и Гбит/с здесь десятичные, заголовки/копии не учтены.

| Профиль | Полезные данные | Решение |
|---|---:|---|
| 4×1280×720 RGB8, 30 FPS | 331 776 000 байт/с = 331,776 МБ/с = 2,654208 Гбит/с | Локальный replay; в 1 Гбит/с не помещается |
| 4×1280×720 NV12, 30 FPS (оценка) | 165 888 000 байт/с = 1,327104 Гбит/с | Также превышает 1 Гбит/с; формат не входит в v1 |
| 4×640×360 RGB8, 30 FPS | 82 944 000 байт/с = 0,663552 Гбит/с | Кандидат отдельного сетевого профиля; измерить запас реального канала |
| Выход 1280×720 RGBA8, 30 FPS | 110 592 000 байт/с = 0,884736 Гбит/с | Локальный IPC, отдельные readback/копирование/upload UI |

При частоте новых ракурсов выше 30 Гц бюджет выхода растёт пропорционально. Сжатие возможно отдельным решением с учётом кодирования/декодирования; смена библиотеки транспорта не добавляет физической полосы. Результаты профилей публикуются раздельно.

## CAN как расширение

В v1 расширение ограничено Classical CAN: channel, can_id, is_extended, DLC 0…8, data длиной DLC и bus timestamp/domain; remote/error frames отвергаются как неподдержанные. CAN FD требует новой capability и правил DLC. Для reverse signal сохраняются CAN ID, битовая позиция, длина, порядок битов, signed/scale/offset и таблица значений. Проверка байтов не заменяет проверку декодирования команды.


## Связанные источники

[[references/README|Единый каталог литературы и документации]].
