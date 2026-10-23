# Остановка источников кадров

`FrameSource::stop()` вызывается внешним владельцем. Для camera/replay/socket несколько
одновременных вызовов допустимы: каждый возвращается после завершения worker shutdown.
Владелец сохраняет объект до возврата **всех** вызовов; concurrent destruction не допускается.
Метод не предназначен для вызова из собственного worker/loader callback.

## Порядок блокировок

Каждый backend имеет отдельный shutdown mutex. Он сериализует stop, `join()` и cleanup.
Source mutex используется кратковременно для установки stopped и синхронизации с
request/wait predicates, затем освобождается **до join**. Worker не берёт shutdown mutex,
поэтому может завершить publish/cleanup, пока владелец ждёт join.

| Backend | Действия при остановке | Что может задержать возврат |
|---|---|---|
| Camera | Stopped под source mutex, wake retry wait, join всех workers | Текущий OpenCV/V4L2 call |
| Replay | Stopped под mutex condition variable, notify, join | Текущий ImageLoader/hash/CPU operation |
| Socket | Stopped и admission под source mutex, io_context.stop, join, close channels/listeners и cleanup paths | Текущий callback io_context |

Раньше replay/socket могли одновременно вызывать join одного std::thread. Atomic stopped
сам по себе не сериализует `joinable()/join()` или cleanup. Отдельный shutdown mutex
исправляет эту гонку; второй stop не возвращает успех до завершения первого.

Replay теперь меняет stopped под тем же mutex, который защищает wait predicates.
Notification не может попасть между проверкой predicate и входом в wait из-за изменения
состояния вне mutex. Это относится к paused/timed replay waits и publish backpressure.

## Границы отмены

Replay проверяет stopped перед очередным camera input и после возврата ImageLoader.
Если stop уже наблюдён, следующий loader не запускается; загруженный после этого
возврата image не проходит validation/hash/cache update и не публикует batch.
Перед publish также проверяется остановка. Очередь не меняется при отменённом publish.
Проверки не прерывают loader либо hash, уже выполняющийся внутри вызова.

В socket request admission и `post` синхронизированы с установкой stopped. Запрос,
допущенный раньше stop, может не выполниться: shutdown не обязан выдавать control ACK
для отменяемой работы. После установленного stop новые requests отвергаются. После join
закрываются и active producers с неполным SV01 сообщением, и listeners. Повторный stop
не удаляет файл, созданный по освобождённому Unix path после первого cleanup.

При bind запоминаются filesystem device/inode Unix socket. Cleanup использует lstat и
удаляет только socket с совпадающей identity: regular file, symlink или другой socket,
подставленный по пути во время работы listener, сохраняется. Это best-effort проверка
владения, не атомарная защита от активной подмены между bind/lstat/unlink; управление
правами родительского каталога остаётся необходимым.

`poll()` после stop может содержать ранее опубликованные frames/control/status events;
stop не является командой очистки всей очереди. Отменённый replay decode не добавляет
нового frame batch. `stats()/poll()` при живом объекте сохраняют прежние mutex semantics.

## Проверки и ограничения

Исправление проверяется blocked-loader test с двумя simultaneous stops, queued-controls
stress и socket test с шестью simultaneous stops, partial producer и повторным cleanup.
Подробности: [[validation/SOURCE_SHUTDOWN]]. Native replay/producer integration продолжает
использовать прежний SV01: новых client commands или config settings нет.

Это синхронизация lifecycle, **не hard deadline**. Бесконечно зависший loader/драйвер
по-прежнему удерживает stop. Для гарантированного прерывания требуется отдельная driver
strategy/process isolation и физическая проверка. Driver timeout/cancellation, mixed
per-camera sources и Aurora acceptance остаются в TODO. См. также [[engineering/SOURCES]].


Общий CTest cap `client_transports` увеличен150→240с: suite объединяет Unix/TCP,
CLI, GUI, scenario и bundle workflows; наблюдавшийся успешный прогон занял149,06с.
Per-operation и subprocess deadlines не менялись. Это запас для полного harness,
не ослабление production timeouts и не вывод о производительности pipeline.
