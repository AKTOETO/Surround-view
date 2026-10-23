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
