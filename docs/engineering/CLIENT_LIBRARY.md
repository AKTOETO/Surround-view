# Универсальная клиентская библиотека: рабочий Unix/TCP API

Linux-профиль 0.6.0 (библиотека введена в 0.5.0). `sv-client-lib` — C++17-библиотека без Qt, OpenCV и GPU; текущие потребители — Qt `sv-client` и CLI `svctl` и тестовый `sv-client-probe`. Общий codec выделен в `sv-wire`. Установленный CMake target — `sv::client`. Проектные требования и дальнейшие операции: [[requirements/CLIENT]], политика listeners — [[requirements/CONFIGURATION]].

Wire-level framing, реальные типы сообщений, command schemas и порядок server calibration workflow вынесены в [[engineering/PROTOCOL_IMPLEMENTED]].

Входы виртуальных камер отделены от клиентских connections: [[engineering/SOURCES]]. В socket source capability `step` не объявляется, команда отклоняется; pause/orbit/resume остаются доступны.

## Настройка сервера

Необязательный корневой блок JSON для Unix-only:

```json
"connections": {
  "unix": {"enabled": true, "directory": "/tmp/sv-blender"},
  "tcp": {"enabled": false},
  "udp": {"enabled": false}
}
```

При явном блоке неописанные транспорты выключены. TCP-only:

```json
"connections": {
  "tcp": {"enabled": true, "address": "127.0.0.1", "control_port": 53101, "data_port": 53102}
}
```

Для удалённого ноутбука указать IP интерфейса сервера вместо loopback; клиент подключается к этому адресу. Допускается одновременное включение Unix и TCP. Сервер принимает один control/data session всего, независимо от числа listeners. Отдельный IPv6 address включает IPv6-only listener; не создаёт дополнительно IPv4 listener. Порты должны различаться и находиться в 1…65535, автоматического выбора/fallback нет. Unix directory — абсолютный путь до 80 байт. При bind failure старт завершается, созданные Unix paths удаляются; чужие файлы сохраняются.

При отсутствии `connections` работает прежний Unix-only профиль и `--ipc-dir DIR`; TCP/UDP не открываются. Явный `connections` запрещает override через `--ipc-dir`, поэтому весь выбор listener хранится в config. `udp.enabled:true` отвергается до открытия listeners: datagram-профиль ещё не реализован. Никакого TCP/UDP fallback библиотека не выполняет.

В сетевом профиле пока нет TLS/аутентификации пользователя. Случайный data token связывает control/data, но не является разрешением пользователя на управление. Эти границы важны для выбора адреса bind; включение IP listener всегда явно.

## Команды запуска

Скопировать config, созданный Blender-конвертером, и добавить нужный `connections`. Сохранить, например, как `artifacts/blender-tcp.json`:

```sh
SV_EGL_PLATFORM=surfaceless build/sv-server \
  --config artifacts/blender-tcp.json \
  --manifest artifacts/blender-street/manifest.json \
  --trace artifacts/blender-tcp-trace.jsonl
build/sv-client --tcp 127.0.0.1 53101 53102
```

Вместо GUI:

```sh
build/sv-client-probe --tcp 127.0.0.1 53101 53102 --frames 3 --exercise
build/sv-client-probe --unix /tmp/sv-blender --frames 3
```

Не запускать оба клиента одновременно: второй session пока не поддерживается. Probe печатает hello, ACK и метаданные кадров как JSON lines; `rgba_fnv1a64` — диагностический checksum буфера, не криптографический хэш. `--exercise` посылает state/pause/top/step и заведомо неизвестную команду для проверки reject. `--allow-reconnect` позволяет пережить временное отключение; probe заканчивает по числу кадров/ACK или общему deadline 15 секунд. Для Qt smoke добавить `--smoke`; Unix GUI сохраняет прежний синтаксис `build/sv-client /tmp/sv-blender`.

## Каталог операций

Точные серверные сценарии, их предусловия, ACK/frame ordering и отказы: [[engineering/PROTOCOL_SCENARIOS]].

| API / событие | Результат / гарантия |
|---|---|
| `Client(Options, Handler)` | Создаёт свой Asio worker и начинает подключение; некорректные options отвергаются синхронно |
| `state()` | Авторитетные paused/view/fusion/revision в ACK без изменения revision |
| `orbit(azimuth_delta_rad, elevation_delta_rad)`, `zoom(distance_delta_m)`, `preset(name)` | Command ID; сервер применяет допустимый ракурс или возвращает reject |
| `pause()`, `resume()`, `step(lease_id = {})` | Управление replay; step переводит на один следующий набор и сохраняет паузу |
| `command(type, parameters)` | Общая точка доступа к опубликованным командам, включая будущие; unsupported command возвращает reject |
| State event | `connecting`, `ready`, `disconnected`, `retry_exhausted` |
| Message event, type 2 | Hello/capabilities сервера |
| Message event, type 21 | ACK/reject с command_id, accepted/reason и state_revision; запрос state возвращает также paused/ракурс/fusion. Ревизия увеличивается после принятой мутации view/source или успешного применения калибровки; rejected и read-only команды её не меняют |
| Message event, type 11 | Неизменяемый `shared_ptr<const Message>` владеет RGBA8 top-left payload и исходными метаданными |
| Error event | Причина транспорта/протокола/таймаута; потерянные pending-команды получают отдельный `session_lost` с command_id |
| `release(frame_header)` | Возвращает frame/session/buffer token; release не уничтожает уже полученный CPU-буфер, старый session не отправляется |
| `stop()` / destructor | Останавливает worker, закрывает sockets; после возврата callbacks не выполняются |

Callback вызывается на worker библиотеки: он должен быстро передать данные своему executor/очереди. Нельзя вызывать blocking `stop()` или уничтожать Client внутри callback; это выполняет владеющий поток. Исключение потребителя перехватывается и не завершает сетевой worker. Буфер можно сохранить после release; никакой Qt/GPU-объект библиотека не создаёт.

Defaults Options: timeout 2000 ms (handshake, partial message, ACK), reconnect delay 1000 ms, max_retries 10 подряд, command_capacity 64. После полной ready-сессии счётчик retries сбрасывается. Command submission, release submission, pending ACK и stream output ограничены; переполнение сообщается как synchronous exception или Error. Команды на disconnect не воспроизводятся; consumer сам решает, посылать ли новую команду. Выбор нового транспорта после ошибки не выполняется. Shutdown отменяет оставшиеся операции; callbacks после stop отсутствуют. `command`/`release` допускают передачу из потоков потребителя; `stop`/destruction должны быть сериализованы владельцем с этими вызовами.

Concurrent command callers сериализуются на участке выдачи command ID, проверки размера и post в worker. Это сохраняет возрастающий wire order; заранее заданного порядка между одновременными вызовами разных потоков нет. После failed submission возможен пропуск ID. Счётчик общий для жизни Client, при reconnect не обнуляется; exhaustion uint64 выдаёт overflow_error и требует нового Client. Проверка `client_command_order` воспроизвела прежний out-of-order reject и проверяет 256 команд от восьми потоков после исправления.

Handshake проверяет codec version и связывает data/session случайным token. Размеры/stride/origin/payload и IDs кадров проверяются до доставки callback. Сервер пока ждёт release 250 ms; медленный потребитель теряет data session и переподключается. Qt-адаптер автомобильного клиента ограничивает отложенные кадры 64, симулятора — 128; ACK/ошибки/lifecycle не отбрасываются из-за frame budget. Адаптер копирует QImage и возвращает release. При новой сессии сервер повторно выдаёт кадр даже на паузе. Время monotonic другого узла не вычитается из локального: GUI показывает server processing time, а physical/network latency требует отдельного clock mapping.

## Использование вне checkout

```sh
cmake --install build --prefix artifacts/install-v05
cmake -S tests/fixtures/client-consumer -B artifacts/external-client \
  -DCMAKE_PREFIX_PATH="$PWD/artifacts/install-v05"
cmake --build artifacts/external-client -j 2
artifacts/external-client/sv-client-probe --tcp 127.0.0.1 53101 53102 --frames 3
```

Consumer использует `find_package(svClient CONFIG REQUIRED)` и `target_link_libraries(app PRIVATE sv::client)`. Устанавливаются `sv/client.hpp`, `sv/protocol.hpp`, static libraries и CMake export/config. Системные dependencies — Boost.JSON/Asio и Threads, без FetchContent. Текущие research RPM включают runtime tools и статическую development-часть в один пакет; разделение на отдельный `-devel` ещё не выполнялось.

## Приёмка и оставшаяся работа

`client_lifecycle` проверяет invalid options, handshake/partial deadlines, неверный RGBA payload и отсутствие callback после stop. `client_transports` запускает настоящий сервер для Unix-only/TCP-only/combined, проверяет отсутствие IP sockets через `/proc`, внешний installed CMake consumer без Qt/OpenCV/EGL/GLES, Qt TCP offscreen, restart/reconnect, pause/step, 500-command paused bursts и нерегулярные replay intervals. `replay_ipc` сохраняет прежний Python/Unix interoperability path. SDK не запускает host Python tests; native lifecycle test не требует GPU.

Проверены localhost Unix/TCP; испытание между двумя физическими машинами остаётся открытым. UDP, несколько одновременных клиентов, типизированные runtime configuration/diagnostic/source-management API, отдельные metrics subscriptions и bindings ещё не реализованы. На сервере есть начальный wire-level workflow `calibrate` → `calibration_status` → `cancel_calibration` / `apply_calibration`, доступный через generic `command()`/`svctl`: `calibrate` требует `points`/`pixels` и отдельные `validation_points`/`validation_pixels`; статус содержит train/validation RMSE, максимум ошибки, gate и базовую revision config. Применение запрещено, когда validation RMSE >3 px, максимальная ошибка >8 px или config изменилась после старта задания. Новая калибровка получает новый `calibration_id`. Пороговые значения не откалиброваны на физическом устройстве и экспериментальны. Jobs принадлежат session ID клиента, отмена/отключение сессии запрещает последующее применение. Работающий OpenCV solver нельзя прервать посередине вызова: отмена кооперативная, результат отбрасывается после возврата solver. Это пока не полноценный доменный API библиотеки. Общий API должен расширяться вместе с server capabilities; универсальность не означает наличие отсутствующих операций.

При использовании RPM каталог библиотек следует архитектурному `%{_lib}`. Если host CMake не ищет `lib64` через `CMAKE_PREFIX_PATH`, передать `-DsvClient_DIR=/usr/lib64/cmake/svClient` (либо фактический путь из `rpm -ql`). Для распакованного пакета указать тот же путь внутри payload prefix; export сохраняет относительные пути и допускает relocation.

Результаты native/host suites, RPM и актуальные полные baselines: [[validation/CLIENT_SMOKE]].

Новый целевой API и процессы: [[architecture/CLIENT_SERVER_MODEL]]. ConfigStore сохраняет применённый calibration JSON и пересоздаёт renderer; held-out gate имеет предварительные лимиты, jobs привязаны к session ID и поддерживают кооперативную отмену. Нельзя прервать текущий вызов OpenCV. Защита от конкурентного apply, control-only и subscriptions ещё не реализованы; generic `command()` не заменяет их серверную реализацию.

## Освобождение кадров при насыщении очереди команд

`release(frame_header)` проверяет строковые поля и размер SV01 metadata синхронно, до счётчика очереди и post. Oversized token/header дают invalid_argument в вызывающем потоке независимо от ready state; rejected submission не занимает release budget. Это соответствует предварительной size validation команд и предотвращает необработанное исключение encode в network worker. На wire type 22 всегда имеет пустой payload.

`command` и `release` имеют независимые bounded submission counters; каждый лимит равен `Options::command_capacity`. Поэтому уже заполненная очередь команд не препятствует постановке RELEASE. Это резерв ёмкости, не приоритет исполнения и не отмена лимитов: заполненная собственная очередь release по-прежнему выдаёт synchronous exception. Data/control write queues также ограничены. Не повторять release произвольно; вызывается один раз для потреблённого кадра. Старый session отбрасывается перед отправкой, shutdown contract не меняется.

GTest/CTest `client_release_budget` с TCP mock проверяет случай capacity=1: worker временно удерживается в frame callback, первая команда заполняет очередь, следующая отклоняется, RELEASE ставится независимо и доставляется на data socket. Повторный RELEASE при занятом release budget отклоняется. До исправления тест воспроизводил `release submission queue full` на первом освобождении. Mock не моделирует реальный server deadline 250 ms или длительную сетевую перегрузку.

## Проверка ACK до завершения команды

Библиотека проверяет `command_id`, boolean `accepted` и строковый `reason` при отказе до удаления команды из pending. Повреждённый ACK завершает сессию как protocol error; ещё ожидающие команды получают Error с `command_id` и `reason=session_lost`. Исход изменения остаётся неизвестным: клиент не должен считать такой ответ отказом сервера или автоматически повторять мутацию. Для принятого ACK поле `reason` не требуется.

Транспорт проверяет базовый конверт; обязательные поля прикладной диагностики дополнительно читают Qt-адаптеры. Исключение при обработке сообщения в GUI теперь останавливает соединение, очищает изображение и состояние и отображает ошибку. Симулятор допускает явное повторное подключение; автомобильный клиент после такой ошибки требует перезапуска. Обычные transport failures продолжают использовать политику reconnect библиотеки. JSON числа в углах, расстоянии и timings допускают целую и дробную запись (`0` и `0.0`). Проверки: [[validation/RESEARCH_RUNTIME]].

`FusionSettings::pyramid_boundary` выбирает `zero` (default) или `normalized` для pyramid fusion. Метод `configure_fusion` опускает исторический default zero при сериализации, normalized передаёт явно. Перед новым вариантом проверить server catalog v3; сервер остаётся владельцем validation/apply. Поддержка в GUI и C++ scenario runner, численные проверки: [[validation/PYRAMID_BOUNDARY]].

### Replay step в исследовательском lease

`step()` сохраняет прежний wire-запрос без token. `step(lease_id)` добавляет token и допускается внутри active replay experiment; перед этим проверить hello capability `experiment_step_v1`. Token не разрешает другим владельцам мутации. Source ACK и frame для его state_revision подтверждают переход, но не обещают seek назад или reset history. Общий C++ runner поддерживает `frames`, typed capture consumer и per-frame baselines; callback вызывается только для measurement frames. Ошибка callback прерывает опыт с попыткой восстановления fusion/surface/pause: [[validation/RESEARCH_RUNTIME]].

### Выбор multilabel seam solver

`FusionSettings::seam_solver` хранит binary_pairs по умолчанию; configure_fusion опускает этот default и явно сериализует alpha_expansion. Проверить catalog v4 перед alpha. Значение доступно общему C++ runner и simulator adapter; сервер единолично валидирует и выполняет solver. Метаданные кадра seam_optimization передаются библиотекой без пересчёта; клиент может видеть convergence, но не трактовать integer energy как качество сшивки: [[validation/MULTILABEL_SEAM]].


## Типизированные output products

Client::output_catalog() и configure_output(OutputProduct::FinalFrame/RenderSupport) управляют session-local selector. RenderSupportView(shared_ptr<const Message>) проверяет формат и предоставляет at(x,y), width/height и исходный message() с provenance без копирования payload. Новые публичные headers client_output.hpp/output_product.hpp/render_support.hpp устанавливаются вместе с svClient CMake package; OpenCV/Qt зависимости у этой части библиотеки нет. CPU-profile consumer и installed external consumer проверены.

Выбор mask требует color view, data ready и idle lease; неизвестные products/неверная schema или pixel values приводят к явной ошибке. Старый final_frame без output_product остаётся совместимым. У type11 render_support_v1 те же release/token правила, что у изображения; сохранять owning pointer после release допустимо. Пример запуска и API: [[engineering/USAGE#Диагностика локальной поддержки через сервер]]. Exact wire contract: [[engineering/PROTOCOL_IMPLEMENTED#Выбор data product: output_selection_v1 / render_support_v1]]. Пока это один выбранный продукт и одна активная сессия, а не parallel multi-client subscription service. GUI mask palette и общий typed registry geometry/source diagnostics остаются открыты.

Полный surface object в `Client::configure_surface` поддерживает optional `source_occlusion_box.uncertainty={radius_m,ambiguous_policy}`; допустимые поля/границы берутся из `surface_catalog.rectangular_source_occlusion.uncertainty`. Server-only validate/apply, revision и lease не изменены. Это runtime исследовательский кандидат, не гарантированная scene visibility: [[validation/NATIVE_UNCERTAINTY]]. Accepted ambiguity доступна через отдельный typed SourceVisibilityView: [[validation/SOURCE_VISIBILITY_PRODUCT]].

`Client::configure_surface` also passes optional bool `source_occlusion_box.require_facing_surface` (defaultfalse; only box-surface mode). Use the advertised catalog, current revision and lease; setfalse/omit on restore. The server owns validation/application. This is a bounded experimental filter, not guaranteed scene visibility. Status9 доступен в source_visibility_v1; GUI form остаётся открытой: [[validation/NATIVE_FACING]], [[engineering/USAGE#Ориентация конечных граней]].

`OutputProduct::SourceVisibility` и `SourceVisibilityView` предоставляют validated owning доступ к четырём camera status bytes на pixel, включая accepted_ambiguous10, accepted_clear11 и footprint_occluder12. at(x,y,camera) проверяет границы; source_visibility_eligible() true только1/10/11. View не копирует payload и не требует OpenCV. Это supplied-geometry policy diagnostics, не scene truth; lifecycle/release прежние. [[engineering/USAGE#Причины отказа и принятая неоднозначность]], [[validation/SOURCE_VISIBILITY_PRODUCT]].

## Управление отображением fallback

`FusionSettings::fallback_display.mode` (`sv::FallbackDisplay`, отдельный installed header) принимает legacy/hatch. После проверки `fusion_catalog.fallback_display` вызвать прежний configure_fusion с текущей revision и полным набором желаемых settings; default legacy опускается для совместимости со старыми серверами. Full apply с omission возвращает legacy, snapshot при legacy может не содержать поле. Mode не изменяет typed diagnostic products и не означает confidence. Simulator имеет кнопку без JSON editing; C++ scenario variants сохраняют nested field. [[validation/FALLBACK_DISPLAY]].

## SourceDisagreementView

Check output_catalog for `source_disagreement_v1`, then `configure_output(OutputProduct::SourceDisagreement)`. Filter type11 by product/revision and release previous in-flight packets. `SourceDisagreementView(message)` owns immutable Message without payload copy/Qt/OpenCV; at(x,y) returns encoded_rgb_range, eligible_cameras, defined, carrier. Undefined means fewer than two eligible sources; it is not zero error or acceptance. Exact schema, shape/provenance, count≤used inputs and domain/defined invariants are validated. release(header) preserves owning view lifetime; bounded server slot still must be released promptly. Selector is temporary, one product per generation, default on reconnect final_frame; active lease forbids switching. [[validation/SOURCE_DISAGREEMENT]].

## Чтение конфигурации через ConfigService

При capability config_read_v1 отправить Client::read_configuration(); accepted ACK разобрать
parse_configuration_snapshot(). Owning ConfigurationSnapshot содержит active/persisted objects,
uint64 revision и flags. Полная серверная validation не дублируется в библиотеке. Scope, limits,
порядок взаимодействия и оставшиеся операции: [[engineering/CONFIG_SERVICE]].

## Каталог и предварительная проверка изменений

При capabilities config_catalog_v1/config_validate_v1 доступны configuration_catalog() и
validate_configuration(base_revision, changes). Owning ConfigurationCatalog содержит operations,
replaceable_groups и fusion/surface parameter catalogs; ConfigurationValidation — base_revision
и проверенные complete replacement groups. Разбирать только accepted ACK методами
parse_configuration_catalog()/parse_configuration_validation(). Для rejected ACK читать reason.
Сервер остаётся authoritative semantic validator; успешная проверка не является apply token.
Пока поддержаны только fusion/surface, без general update/persistence. [[engineering/CONFIG_SERVICE]].

## Применение конфигурации сервером

После read_configuration()/configuration_catalog() доступны validate_configuration(base, changes) и apply_configuration(base, changes, persist). Fusion/surface передаются полными replacement groups. persist=false оставляет файл неизменным; persist=true сохраняет весь active snapshot. Ответ разбирается parse_configuration_application(); не повторять mutation после потери ACK автоматически. GUI binding общего сервиса ещё предстоит; команды CLI, диаграмма, ограничения lease и проверки: [[engineering/CONFIG_SERVICE]].

## Результат операции после потери ACK

Для новых apply использовать explicit apply_configuration_once(instance, operation_id, base, changes, persist) и configuration_operation_status(instance, operation_id), затем parse_configuration_operation(). ID/intent хранит приложение; библиотека не повторяет mutation автоматически. Журнал сохраняется при reconnect, ограничен128 операциями и теряется при restart; старый instance нельзя автоматически заменять новым. Команды CLI, sequence diagram и ограничения: [[engineering/CONFIG_OPERATIONS]].

## Devel telemetry_v1 (15.10.2026)

`Client::telemetry_catalog()` и `Client::read_telemetry(instance, epoch, after_cursor, limit=16)`
возвращают command IDs; accepted ACK в callback разбирается owning typed parsers
`parse_telemetry_catalog` / `parse_telemetry_batch` из installed `client_telemetry.hpp`.
Это process-scoped bounded polling default-off collector, без Qt/OpenCV dependencies и без
автоматического enable/retry. Instance/epoch сверяются вызывающим приложением с ожидаемым запросом;
parser проверяет envelope, window/cursors, units, finite metrics и исторические revisions.
После reconnect перечитать catalog; новые instance/epoch начинают отдельную серию.
Настройки применяет только сервер через ConfigService; control/data пока обязательны.
Wire fields, fresh/loss semantics, CLI и ограничения: [[engineering/TELEMETRY]].

## Локальный archive codec

Installed `client_telemetry_archive.hpp` предоставляет TelemetryArchive и serialize/parse API для
bounded window snapshot. Нет Qt/file I/O/server mutation. Поля, units,120-record/2MiB limits,
проверки cursor/time/loss и отличия от experiment trace: [[engineering/TELEMETRY_ARCHIVE]].

Optional `TelemetrySample::source_work` содержит owning per-camera DTO из installed
`source_work_timing.hpp`; Qt/OpenCV dependency не добавлена. Старое отсутствие поля допустимо;
validation и archive compatibility: [[engineering/SOURCE_TIMING]].


Optional TelemetrySource::camera_diagnostics содержит четыре CameraDiagnostics (public installed camera_diagnostics.hpp): exact counters и worker_stopped. Старые сообщения без поля и null принимаются; malformed array/order/types/range отклоняются. Поля доступны без fresh frame. [[engineering/SOURCES#Диагностика состояния каждой аппаратной камеры]].


CameraDiagnostics::negotiated_mode содержит optional CameraCaptureMode (installed camera_capture_mode.hpp): width/height, exact open_attempt и nullable checked fps/fourcc/buffer_count. SDK отвергает stale attempt и mode у stopped worker, принимает omission/null от прежнего сервера. Это snapshot источника, не metadata выбранного Frame. [[engineering/SOURCES#Проверенный режим камеры через SDK]].
