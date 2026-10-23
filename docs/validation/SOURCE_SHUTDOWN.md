# Проверка конкурентной остановки источников

23.10.2026. Контракт: [[engineering/SOURCE_LIFECYCLE]]. Исправление относится к внутреннему
FrameSource lifecycle, не добавляет SV01 commands и не квалифицирует физический драйвер.

Выявлены по коду: concurrent join/cleanup у replay/socket, изменение replay stopped вне
mutex CV predicates и отсутствие stop check после ImageLoader return. Atomic flag не
сериализует std::thread::join. Исправлены mutex order и проверки на границах операций.

| Regression case | Проверяемые postconditions |
|---|---|
| Blocked replay loader + два stop | Оба ждут освобождения loader; всего1 loader call, decoded/received0, нет нового batch; requests после stop отвергаются |
| Replay queued controls / paused lifecycle,12 повторов | Оба stop завершаются; нет double join; новые requests отвергаются |
| Socket partial SV01 + шесть stop,12 повторов | Все stop завершаются; peer получает EOF/reset; listeners/paths закрыты; repeated stop сохраняет replacement file |

Первый source work suite59/59 (включая первые3 lifecycle cases) прошёл до общего
integration run за3,12с. После этого добавлен четвёртый lifecycle case: replacement
regular file/symlink/socket при ещё активном listener должен пережить первый stop
и destructor. Это проверяет type/device/inode guard, а не только repeated cleanup.
Многопоточные stress controls дополняют проверку порядка mutex по коду, но не являются
исчерпывающим перебором всех interleavings или доказательством hard real-time deadline.
Blocked mock имеет только тестовый timeout; real ImageLoader не прерывается этим кодом.

Остаются backend/driver cancellation, ThreadSanitizer qualification и физические
V4L2/Aurora испытания. Сохранность объекта до завершения всех stop callers обязательна;
тесты не допускают concurrent destruction или вызова stop из собственного worker.


Первый integration run7/7 за181,89с прошёл до добавления ownership guard: sources0,12с,
camera_sources16,00с, experiment_watchdog2,88с, replay_ipc5,94с,
client_transports149,06с, telemetry_ipc4,79с, source_work_timing3,10с. Затем общий
client_transports cap увеличен150→240с; subprocess/request timeouts не менялись.
Повтор всего69-test suite не заявляется. Identity check не является атомарным
security boundary при активно меняющемся родительском каталоге.


После ownership guard и финальной сборки повторены затронутые3 CTest:3/3 за20,62с
(sources0,16с, camera_sources17,26с, source_work_timing3,13с). Source suite60/60,
включая4 новых lifecycle cases. Generated CTest property подтверждает timeout240;
полный transport harness после изменения этого cap повторно не запускался.
Все76 PlantUML-вставок прошли local syntax check; git diff --check успешен.
