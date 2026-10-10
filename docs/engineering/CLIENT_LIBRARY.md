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

| API / событие | Результат / гарантия |
|---|---|
| `Client(Options, Handler)` | Создаёт свой Asio worker и начинает подключение; некорректные options отвергаются синхронно |
| `state()` | Авторитетные paused/view/fusion/revision в ACK без изменения revision |
| `orbit(azimuth_delta_rad, elevation_delta_rad)`, `zoom(distance_delta_m)`, `preset(name)` | Command ID; сервер применяет допустимый ракурс или возвращает reject |
| `pause()`, `resume()`, `step()` | Управление replay; step переводит на один следующий набор и сохраняет паузу |
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

`command` и `release` имеют независимые bounded submission counters; каждый лимит равен `Options::command_capacity`. Поэтому уже заполненная очередь команд не препятствует постановке RELEASE. Это резерв ёмкости, не приоритет исполнения и не отмена лимитов: заполненная собственная очередь release по-прежнему выдаёт synchronous exception. Data/control write queues также ограничены. Не повторять release произвольно; вызывается один раз для потреблённого кадра. Старый session отбрасывается перед отправкой, shutdown contract не меняется.

GTest/CTest `client_release_budget` с TCP mock проверяет случай capacity=1: worker временно удерживается в frame callback, первая команда заполняет очередь, следующая отклоняется, RELEASE ставится независимо и доставляется на data socket. Повторный RELEASE при занятом release budget отклоняется. До исправления тест воспроизводил `release submission queue full` на первом освобождении. Mock не моделирует реальный server deadline 250 ms или длительную сетевую перегрузку.
