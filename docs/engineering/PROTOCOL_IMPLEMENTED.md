# Реализованный протокол SV01

Срез реализации Linux 0.6.x. Это описание текущего wire-контракта, сверенное с `include/sv/protocol.hpp`, `src/core/protocol.cpp`, `src/apps/server.cpp` и `src/client_library/client.cpp`. Runtime fusion API `fusion_runtime_v1` описан ниже; общий ConfigService ещё не реализован. Целевая расширенная архитектура и подписки описаны отдельно в [[architecture/CLIENT_SERVER_MODEL]] и пока не являются возможностями сервера.

## Транспорт и кадрирование

Один логический клиент использует две независимые stream-связи: control и data. Поддерживаются Unix domain sockets либо TCP; транспорт выбирается только конфигурацией endpoint/listeners. Клиентская библиотека сама не переключается на другой транспорт. UDP, TLS, несколько одновременных клиентов и control-only пока отсутствуют. Серверная сторона ограничена одной сессией: второй клиент не образует независимое пространство состояния.

Сообщение содержит 24-байтовый префикс: ASCII `SV01`, версия 1, тип uint16, полная длина заголовка uint32, длина payload uint64, flags uint32=0; числа big-endian. За ним следуют JSON UTF-8 и binary payload. Пределы — 64 KiB header и 64 MiB payload. TCP-чтение не совпадает с границами сообщений, поэтому decoder буферизует частичные и пакетированные сообщения.

| Тип | Направление и смысл в текущем коде |
|---:|---|
| 1 | Клиент → сервер: hello; на control `{role:"control"}`, на data `{role:"data", session_id, data_token}` |
| 2 | Сервер → клиент: hello_ack с `session_id`, `data_token`, `profile`, `capabilities` |
| 3 | Библиотека → callback: локальное событие потери ожидающей команды (`session_lost`); отдельный wire error пока не реализован |
| 11 | Сервер → клиент по data: финальный RGBA8 кадр и provenance/timing metadata |
| 20 | Клиент → сервер по control: команда с монотонным в рамках сессии `command_id`, `type` и параметрами |
| 21 | Сервер → клиент по control: ACK/reject с `accepted`, `reason`, `state_revision` и результатом операции |
| 22 | Клиент → сервер по data: release с `session_id`, `frame_id`, `buffer_token` |

## Переключение fusion во время работы: fusion_runtime_v1

Handshake объявляет `fusion_runtime_v1`. Это первый реализованный срез исследовательского runtime, не общий ConfigService. Поверх него реализован application-level paused-frame runner через sv-client-lib: [[validation/RESEARCH_RUNTIME]]. Сервер сохраняет все три текущих GPU-режима; для переключения не требуется перезапуск, повторный upload камер или rebuild сетки.

| Команда / поле | Контракт |
|---|---|
| `fusion_catalog` | Read-only ACK с `fusion_catalog`: version=1, modes, diagnostics, пределы параметров, temporary persistence, backend/GL device и server source revision/fingerprint |
| `configure_fusion` | Обязательные `base_config_revision` (decimal string) и `fusion` (object); полностью заменяет fusion-секцию, пропущенные необязательные поля получают defaults |
| `fusion.mode` | `edge_feather`, `hard_best_angle`, `angular_feather` |
| `fusion.diagnostic` | `color` (default), `coverage`, `weights` |
| `fusion.edge_width_px` | Число (0,4096], default 24 |
| `fusion.angle_power` | Число (0,32], default 2 |
| ACK / `state` | `config_revision` и полный `fusion` вместе с существующими state fields |
| type 11 frame | `config_revision` и полный фактический `fusion`; `state_revision` позволяет отфильтровать ранее поставленные в очередь кадры |

`sv-client-lib` предоставляет `fusion_catalog()` и `configure_fusion(base_revision, FusionSettings)`. Неизвестные режимы, поля fusion, неконечные числа и параметры вне диапазона отклоняются общей config validation. Устаревшая revision отклоняется; rejected/read-only команды не меняют revisions. Запрос с корректными параметрами и старой revision возвращает `stale_config_revision`; ошибочный запрос может быть отклонён раньше при validation.

Команда выполняется на render thread между render calls: полный проверенный snapshot и uniform settings становятся активными до следующего render. Входной frame set при паузе сохраняется; новый результат может использовать тот же набор камер с новой config revision. ACK подтверждает применение, но уже отправленные/очередные старые кадры не отзываются. Переключение не делает `frame_set_id` новым и не является полноценным seek/reset истории.

`configure_fusion` не записывает файл конфигурации; restore выполняется отправкой ранее прочитанного полного fusion с текущей revision. Автоматический restore при disconnect, experiment lease, идемпотентные operation IDs и отдельный save API ещё отсутствуют. Существующий `apply_calibration` сохраняет полный актуальный snapshot, поэтому при его использовании временный fusion также попадёт в файл: до общего ConfigService восстановить baseline перед persistent calibration apply. Это ограничение не следует скрывать в автоматических сценариях.

Исследовательские graph-cut/multiband режимы пока остаются offline и не объявляются каталогом. Следующий этап — расширять набор серверных реализаций с parity tests, сохраняя runtime-вариативность на целевой платформе: [[architecture/RESEARCH_RUNTIME]].

## Порядок открытия сессии и доставки кадра

```plantuml
@startuml
participant "sv-client-lib" as C
participant "control.sock / TCP control" as CT
participant "sv-server" as S
participant "data.sock / TCP data" as DT
C -> CT : type 1 {role: control}
CT -> S : hello control
S --> CT : type 2 {session_id, data_token, capabilities}
C -> DT : type 1 {role: data, session_id, data_token}
DT -> S : bind data channel
S --> DT : type 2 hello_ack
C -> CT : type 20 {command_id, type: state}
CT -> S : validate command order
S --> CT : type 21 {accepted, state_revision, view}
S --> DT : type 11 {frame_id, inputs, timings} + RGBA8
C -> C : copy/consume payload
C -> DT : type 22 {session_id, frame_id, buffer_token}
note over S,DT
  Единственный выходной слот закрывается после release.
  Deadline release — 250 ms; таймаут закрывает data session.
end note
@enduml
```

Если release теряется или задерживается, библиотека/клиент не получает второй кадр в этом stream; по deadline сервер закрывает соединение. Отправлять release нужно после копирования данных в собственный буфер. Client callback работает на worker thread; UI обязан передать событие в свой executor и не блокировать остановку клиента внутри callback.

## Команды

Все команды имеют заголовок `{command_id: decimal-string, type: string, ...parameters}`. Повторный или меньший ID отклоняется как `duplicate_or_out_of_order`. ACK несёт `command_id`, `accepted`, при отказе — `reason`; состояние/результат включается в ACK. Потерянные при разрыве pending-команды получают локальную ошибку `session_lost` и автоматически не воспроизводятся после reconnect.

| Команда | Параметры | Эффект / отказ |
|---|---|---|
| `state` | нет | Read-only view, fusion, source_type, pause и state/config revisions |
| `orbit` | `azimuth_delta_rad`, `elevation_delta_rad` | Изменение virtual view с ограничением elevation и проверкой clearance |
| `zoom` | `distance_delta_m` | Изменение дистанции, диапазон 6–18 m |
| `preset` | `name=top/front/rear` | Переключение стандартного ракурса; иначе `unknown_preset` |
| `pause`, `resume`, `step` | нет | Управление replay; для не-replay `step_unsupported_for_source` |
| `calibrate` | см. ниже | Асинхронная job, результатом ACK является `job_id` |
| `calibration_status` | `job_id` | Только владелец сессии; статус, fit/validation metrics |
| `cancel_calibration` | `job_id` | Только владелец; running solver отменяется кооперативно после возврата OpenCV |
| `apply_calibration` | `job_id` | Только владелец; новый renderer подготовлен до сохранения, проверяется base revision и held-out gate |

Неизвестная операция возвращает `unknown_command`; malformed/non-finite parameters возвращают reject с причиной. Config revision/state revision не меняются на read-only и rejected операциях. Калибровка применима только если validation RMSE ≤3 px, max error ≤8 px и базовая revision ещё актуальна. Лимиты 3/8 px предварительны и должны быть пересмотрены на физических данных.

Пример сокращённого запроса калибровки:

```json
{
  "command_id": "17",
  "type": "calibrate",
  "camera_id": 0,
  "method": "ransac_epnp_lm",
  "points": [[1.0, 0.2, 0.1], [2.0, -0.4, 0.2]],
  "pixels": [[413.2, 250.1], [382.0, 214.4]],
  "validation_points": [[1.5, 0.1, 0.4], [3.0, 0.7, 0.2]],
  "validation_pixels": [[400.0, 230.0], [355.0, 190.0]],
  "provenance": {
    "dataset_id": "survey-2026-10-10",
    "training": {
      "observation_ids": ["front-f0001-c00", "front-f0001-c01"],
      "frame_ids": ["front-f0001", "front-f0001"]
    },
    "validation": {
      "observation_ids": ["front-f0020-c00", "front-f0020-c01"],
      "frame_ids": ["front-f0020", "front-f0020"]
    }
  }
}
```

Фактически сервер требует 6..10000 train и validation соответствий и обязательный `provenance`; координаты точек имеют три конечные метрические компоненты, пиксели — две. Пример выше показывает только форму и намеренно не является исполняемым минимумом. Источник, единицы, независимое измерение, detector, hashes observations и распределение поз остаются обязанностью вызывающего инструмента. GUI не должен генерировать псевдоизмерения.

С 10.10.2026 handshake объявляет capability `calibration_provenance_v1`. Старые calibration requests без provenance отклоняются с `calibration_provenance_required`; транспорт SV01 и остальные команды сохраняют формат. Ограничение 10000 — лимит менеджера; фактический SV01 header limit 64 KiB дополнительно ограничивает размер wire запроса. Для каждого split `observation_ids` и `frame_ids` идут в том же порядке, что XYZ/UV, с той же длиной. IDs — 1..128 ASCII символов `[A-Za-z0-9._:/-]`, непрозрачные токены, не пути к серверным файлам. `dataset_id` обозначает набор; observation ID уникален во всём запросе. Несколько углов одного кадра имеют разные observation IDs и одинаковый frame ID. Исходный frame ID должен сохраняться при crop/augmentation/re-detection, иначе клиент скрывает пересечение данных.

До выделения job ID/queue сервер проверяет:

- размеры metadata, конечность XYZ/UV и valid pixels;
- уникальность observation IDs внутри и между split;
- отсутствие общих frame IDs между train/validation;
- отсутствие точных копий `(X,Y,Z,u,v)` внутри и между split независимо от IDs.

Одна физическая XYZ точка в разных кадрах с различными UV допустима. Погрешность сравнения exact-content равна нулю; signed zero считается одинаковым. Near duplicates или произвольно изменённые labels не распознаются этой проверкой. Присланные клиентом IDs не аутентифицируют снимки и не доказывают статистическую независимость.

ACK принятой calibration job и `calibration_status` возвращают `validation_policy="client_declared_frames_and_exact_content_disjoint"`. Status дополнительно содержит `dataset_id`, `training_observations`, `validation_observations`, `training_frames`, `validation_frames` во всех состояниях job. В job сохраняется компактная сводка, а не исходные metadata arrays; оригинальный dataset/request клиент должен архивировать сам. Пороговые gate значения 3/8 px остаются предварительными.

Явные причины reject: `calibration_provenance_required`, `calibration_provenance_size_mismatch`, `calibration_provenance_invalid_id`, `calibration_duplicate_observation_id`, `calibration_train_validation_frame_overlap`, `calibration_duplicate_correspondence`, `calibration_invalid_correspondence`, `calibration_nonfinite_correspondence`. Некорректные JSON типы/отсутствующие вложенные поля также отклоняются, но текст исключения JSON parser не является стабильным error code. Проверки и пределы: [[validation/CALIBRATION_PROVENANCE]].

```plantuml
@startuml
actor "Оператор" as U
participant "sv-simulator / svctl" as A
participant "sv-client-lib" as L
participant "sv-server" as S
participant "CalibrationJobManager" as J
participant "ConfigStore + Renderer" as R
U -> A : запустить calibrate с train и held-out observations
A -> L : command(calibrate, data)
L -> S : type 20
S -> J : validate IDs / frame split / exact content
alt malformed or overlapping observations
  J --> S : reject before enqueue
  S --> A : type 21 accepted=false + reason
else disjoint declared inputs
  S -> J : submit(owner_session, base_revision)
S --> A : type 21 accepted + job_id + validation_policy
loop пока job выполняется
  A -> L : calibration_status(job_id)
  L -> S : type 20
  S -> J : get(owner_session, job_id)
  S --> A : type 21 state + metrics
end
alt job отменена или качество не прошло gate
  A -> L : cancel_calibration(job_id) / не применять
  L -> S : type 20
  S -> J : cancel / status
  S --> A : type 21; конфигурация не меняется
else оператор применяет результат
  A -> L : apply_calibration(job_id)
  L -> S : type 20
  S -> R : revision check → prepare renderer → atomic persist
  R --> S : commit либо reject; старое состояние при ошибке
  S --> A : type 21 applied/reject + state_revision
end
end
@enduml
```

*Рисунок — Calibration workflow: reject до enqueue, status и отдельное применение только для созданной job.*

## Метаданные и гарантии

Кадр 11 включает `frame_id`, `frame_set_id`, `state_revision`, `applied_command_id`, token, размер/stride, `RGBA8`, top-left origin, source/fusion/view IDs, четыре записи `inputs`, health и spans pipeline. Он не является промежуточной stitched texture и не содержит control subscriptions. `local_monotonic` timestamp нельзя вычитать из часов удалённого ноутбука; текущие показатели server-side, а не физическая sensor-to-display задержка.

Очереди и payload bounded. Network delivery не подтверждает источник-съёмку: текущий camera source timestamp соответствует host delivery, если драйвер не предоставляет проверенный capture timestamp. Утилита должна сохранять endpoint, версии, config/calibration revision, IDs и hashes данных в отчёте эксперимента.

## Планируемое расширение и проверка

Несколько независимых клиентов, control-only, per-session view, config snapshot/update, subscriptions (`final_frame=false`), canvas/coverage/weights, trace stream, typed library API, UDP и двуххостовой clock mapping не реализованы. Их normative target contract — [[requirements/PROTOCOL]] и [[architecture/CLIENT_SERVER_MODEL]]. Protobuf не нужен для текущего небольшого JSON-header/binary-frame профиля: менять codec следует при появлении совместного schema/codegen требования, а не ради замены формата.

Основные тесты текущего протокола: `client_lifecycle`, `client_transports`, `server_session`, `calibration_job`, `config_store`, `replay_ipc`. Пробелы: fuzz/property decoder, контрольные vectors для каждого message type, command ID overflow/wrap policy, длительный release/backpressure, cancellation race, и межмашинное TCP испытание.
