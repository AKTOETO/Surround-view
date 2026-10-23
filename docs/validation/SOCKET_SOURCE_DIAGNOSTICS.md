# Проверка per-camera socket producer diagnostics

23.10.2026. Контракт: [[engineering/SOCKET_SOURCE_DIAGNOSTICS]]. Проверяются настоящие
локальные producer sockets и metadata SDK; физический capture/Aurora не квалифицируются.

C++ cases: валидный HELLO и bounded mailbox drop; paused frame drops; busy refusal с
сохранением действующего session; duplicate-sequence rejection; reconnect с новой session;
peer EOF; partial-handshake timeout без ложного reconnect; source stop category.
Typed SDK/archive тестируют session/counters/capability, legacy omission и8 malformed
claims. Максимальные uint64 snapshots проходят bounded pagination без потери16 samples.
При20 rejected connections status mailbox хранит16 events, а diagnostics сохраняют20.

Qt runtime/editor/telemetry suite27/27 прошёл до общего прогона: socket state виден без
fresh frame; unknown/null не становится zero; большие uint64 не округляются в graph,
details сохраняет session и reason/counters. Preflight CTest2/2 за19,10с включает
camera_sources15,97с и source_work_timing3,13с (до финального mailbox-saturation case).

Native Python Unix/TCP harness включает telemetry socket source: проверяет ready session,
paused drops всех4 cameras и reconnect после rejection. Python — только test oracle,
продуктовые counters/state/SDK реализованы в C++. Полная event history, capture-to-display
latency, независимый sampler, overhead levels и аппаратная платформа остаются открытыми.


Финальная Linux-сборка успешна. Полный CTest69/69 за197,91с:
camera_sources16,10с, client_transports123,84с, telemetry_ipc3,37с,
source_work_timing3,15с (66/66 GTest, включая6 socket diagnostics cases),
simulator_configuration_qml10,12с. Все78 PlantUML-вставок прошли syntax check;
py_compile изменённых Python tests и git diff --check успешны.

SDK установлен в /tmp/sv-socket-diagnostics-install. Отдельный C++17 CMake consumer
использует find_package(svClient CONFIG REQUIRED)/sv::client, включает установленный
socket_source_diagnostics.hpp, вычисляет reconnects, сериализует4 snapshots и линкует
SDK telemetry parser. Configure/build/run успешны, exit0. Это host installation check,
не Aurora RPM/ABI или физическая camera qualification.

## Проверка отмены deadline

23.10.2026: исправлена обработка уже готового timeout callback после cancel/rearm.
Четыре GTest в `tests/socket_deadline_tests.cpp` используют детерминированную очередь
таймера: successful callback после cancel; старые successful/aborted callbacks после
rearm; destruction/failed rearm; ошибка native cancel. Проверяется также срабатывание
нового актуального deadline. Это воспроизведение порядка callbacks, а не измерение
частоты гонки в production или на конкретной версии Boost.

Настоящий Unix socket case в `tests/socket_diagnostics_tests.cpp` передаёт четыре
фрагментированных кадра, проверяет idle дольше timeout после полного сообщения,
затем timeout неполного кадра, независимость соседней камеры и reconnect с новым HELLO.
Реальный socket test дополняет mock; он не форсирует очередь ready callbacks.

Linux build успешен. Финальный профильный прогон:
`ctest --test-dir build -R '^(sources|camera_sources|source_work_timing)$' --output-on-failure -j1`
— 3/3 за19,85с, source GTest71/71 (включая четыре deadline cases и новый socket case).
Прямой запуск SocketDeadline.* также4/4. Полный69-test прогон выше относится к предыдущей
версии; в этой проверке весь CTest повторно не запускался. Hardware/Aurora и overhead
не проверены; протокол и публичный SDK не изменены.

Все80 PlantUML-вставок в архитектуре и главе3 прошли syntax check; git diff --check успешен.
