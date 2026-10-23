# Проверка последнего неуспешного вызова камеры

23.10.2026. Проверяется engineering telemetry, не производительность физической камеры.
Контракт: [[engineering/SOURCE_TIMING#Последний неуспешный вызов камеры]].

| Mock case | Ожидаемый operation / outcome |
|---|---|
| is_open throws | is_open / opencv_exception |
| open returns false | open / false_return |
| open throws | open / opencv_exception |
| read returns false | read / false_return |
| read returns true with empty Mat | read / empty_frame |
| read throws | read / opencv_exception |
| read fails, close throws | close / opencv_exception (последний сбой) |

5 GTest cases охватывают семь вариантов на четырёх workers, disabled measurement,
замену session во время blocked mock read с **тем же** числовым epoch, typed parser,
archive round-trip и legacy omission. Проверяются девять malformed combinations:
policy/epoch/event/operation/outcome/interval/future completion/duration/extra field.
Collector filtering проверяется отдельно; максимальные uint64 diagnostics snapshots
пагинируются без потери16 samples и не превышают текущий reply budget.

Qt runtime/editor/telemetry suite26/26 прошёл до полного прогона: новый failure gauge
доступен без fresh render, null сохраняется, details содержат event/operation/outcome,
summary не вычисляет p50 из повторов одного state. Python transport test проверяет
advertised capability на настоящем server, но physical failure payload проверяется
через C++ CameraCapture mock seam. Эти уровни свидетельств не смешиваются.

Физические камеры, driver hang/cancellation, worst-case latency и Aurora не проверены.
Mock sleep используется только для ненулевого wall interval, не как performance benchmark.
Source snapshots удерживают один последний failure на камеру, а не весь поток событий.


Полная финальная Linux-сборка успешна. CTest69/69 за247,60с:
client_transports133,10с, telemetry_ipc4,85с, source_work_timing3,04с,
simulator_configuration_qml7,87с. Все74 PlantUML-вставки прошли syntax check;
py_compile изменённого Python test и git diff --check успешны.

После тестов выполнены `cmake --install build --prefix /tmp/sv-camera-failure-install`
и отдельный CMake consumer (`find_package(svClient CONFIG REQUIRED)`, `sv::client`,
C++17). Он включает установленный camera_failure_timing.hpp/client_telemetry.hpp,
создаёт CameraDiagnostics.latest_failure, сериализует record и линкует SDK parser.
Configure/build/run успешны, exit0. Это host install/consumer check, не Aurora RPM/ABI
qualification. Дополнительные performance выводы из времени regression suite не делаются.
