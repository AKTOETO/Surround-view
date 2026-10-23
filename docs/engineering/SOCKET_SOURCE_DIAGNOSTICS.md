# Диагностика socket producers по камерам

При `source.type=socket` сервер ведёт четыре независимых снимка соединения и счётчиков.
Они доступны через прежние telemetry API sv-client-lib; новых команд SV01 нет.
Поддержка объявлена `telemetry_catalog.socket_source_diagnostics_v1=true`, данные —
`telemetry.samples[].source.socket_diagnostics` (null либо ordered array camera0…3).
Replay/camera backends оставляют поле null, отсутствие legacy поля допустимо.

## Состояние соединения

Каждый элемент содержит camera_id, counters и connection:

| connection поле | Значение |
|---|---|
| state | disconnected / handshaking / ready |
| session_id | Не null только в ready; совпадает с session_id валидированного producer HELLO |
| last_close_reason | Последнее закрытие, даже после reconnect; null до первого close |

`ready` означает, что HELLO прошёл validation; не гарантирует доставку ACK, наличие
кадров, синхронность rig или пригодность финального изображения. Смена state/связанных
полей выполняется под одним source mutex. Close reasons: peer_disconnected,
handshake_write_failed, protocol_rejected, message_timeout, source_stopped.
Trace сохраняет прежнее подробное сообщение, снимок — фиксированную категорию.

Producer state и frozen frame — разные сущности. При pause выбранные изображения
сохраняются по прежней политике, но producer может отключиться или подключиться с
новой session. Теперь это видно в текущей диагностике. Политика TTL/health и очистка
synchronizer здесь не меняются; `frame.health=READY` не равно live producer ready.

## Счётчики

Все counters — decimal uint64 strings на wire и uint64 в SDK:

| Counter | Что учитывает |
|---|---|
| accepted | Соединения, допущенные в свободный camera slot |
| refused | Новые соединения к занятому slot; действующий producer сохраняется |
| handshakes | Валидированные HELLO, до гарантии доставки ACK |
| closed | Закрытые допущенные соединения, включая server stop |
| rejected | Закрытия из-за decoder/metadata/sequence/payload rejection |
| timeouts | Закрытия по message deadline, включая partial HELLO |
| received | Валидированные кадры до проверки pause/очереди |
| dropped | Pause drops и вытеснение старого кадра из bounded source mailbox |

`counters.reconnects()` в SDK вычисляет max(handshakes−1,0). Это число повторных
валидированных HELLO, не число reconnect attempts. Bad/partial handshake не увеличивает
его. Socket read errors/EOF и ACK write failure видны в last_close_reason/closed.
Sync/render/network drops не входят в source dropped; их стадии учитываются отдельно.

Counters живут вместе с source object и не сбрасываются при telemetry epoch. Они
обновляются и при выключенной telemetry: enabled управляет sampling/history, а не
состоянием backend diagnostics. Нулевой overhead не заявлен; detail levels и overhead
measurement остаются в TODO. Global source counters и per-camera snapshot могут
отражать короткий переход между этапами обработки; их разность не является метрикой потерь.

## SDK, archive и GUI

`SocketSourceDiagnosticsSet` доступен в `TelemetrySource::socket_diagnostics`.
Публичный header устанавливается вместе с sv-client-lib. SDK проверяет ordered IDs,
точные fields/types, enum state/reason, bounded session_id и согласованность counters:
closed≤accepted, active slot0/1, session только в ready, dropped≤received,
rejection/timeout counts не превышают closed. Неизвестные/повреждённые claims отклоняются;
legacy omission/null читаются. Archive schema1 сохраняет snapshot и capability.

В **Телеметрия → Источники** доступны `Socket N` metrics: connected, received, dropped,
reconnects, timeouts, rejected. Connected=1 включает handshaking; details различают фазы
и показывают session, last reason, accepted/refused/handshakes/closed. Без backend данных
значение null, а не ноль. Counters выше2^53 не округляются в графике; точные uint64
сохраняются в SDK/archive/details. Эти cumulative gauges не являются частотами ошибок
или latency distributions.

Изменения counters/state происходят до постановки status event. Если bounded status
mailbox переполнен и часть trace events пропущена, текущая диагностика сохраняется.
Это не event history: последовательность промежуточных состояний между samples может
быть потеряна. History/read pagination сохраняет прежние byte/count budgets.

Проверки: [[validation/SOCKET_SOURCE_DIAGNOSTICS]]. Диаграмма:
[[architecture/RESEARCH_RUNTIME#Состояние socket producer отдельно от кадра]], диплом §3.77.
