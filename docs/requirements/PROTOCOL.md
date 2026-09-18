# Требования к протоколу и данным

## Общие требования

| ID | Приоритет | Требование | Приемка |
|---|---|---|---|
| PRO-F-001 | MUST | Каждое сообщение должно иметь `protocol_version`, `message_type`, `source_id`, `sequence_id`, `timestamp_ns`, `payload_size`. | Некорректный header отклоняется и логируется. |
| PRO-F-002 | MUST | Timestamp должен быть монотонным внутри источника. | Нарушение порядка фиксируется telemetry. |
| PRO-F-003 | MUST | Версия payload должна позволять отклонить несовместимое сообщение. | Старый/неизвестный version дает явную ошибку. |
| PRO-F-004 | MUST | Кадр камеры должен содержать ID, resolution, pixel format и payload. | Кадр можно декодировать без внешнего контекста. |
| PRO-F-005 | MUST | CAN frame должен содержать channel, CAN ID, extended flag, DLC и bytes. | Payload восстанавливается побайтно. |
| PRO-F-006 | MUST | UI command должен содержать command ID, тип, параметры и timestamp. | Команда подтверждается или отклоняется. |
| PRO-F-007 | MUST | Rendered frame должен содержать frame ID, resolution, format и timestamps. | UI определяет свежесть кадра. |
| PRO-F-008 | MUST | Сервер должен различать malformed, stale, duplicate и dropped messages. | Для каждого класса есть telemetry counter. |
| PRO-F-009 | MUST | Размеры сообщений и очередей должны иметь ограничение. | Аномальный payload не приводит к неограниченному выделению памяти. |
| PRO-F-010 | SHOULD | Control-сообщения и крупные binary payload должны иметь независимые очереди. | Поток кадров не блокирует UI commands. |

## Политика кадров

- Для каждого `camera_id` поддерживается отдельная очередь.
- Устаревший кадр определяется по настраиваемому timeout.
- При рассинхронизации сервер применяет выбранную политику: последний валидный кадр или diagnostic placeholder.
- Политика должна быть видна в telemetry.

## Политика транспорта

MVP должен использовать отлаживаемый транспорт: TCP/loopback TCP или ZeroMQ over TCP. Переход к shared memory, UDP/RTP, DMA-BUF или zero-copy разрешен только после baseline измерений.

## Безопасность границ

- Проверять длину header и payload до чтения.
- Проверять диапазоны `camera_id`, `DLC`, resolution и pixel format.
- Отбрасывать повторно использованный или переполненный sequence number по политике протокола.
- Не выполнять shader или конфигурационный код, пришедший от внешнего клиента.
