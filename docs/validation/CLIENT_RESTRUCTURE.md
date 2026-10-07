# Первый этап новой клиентской архитектуры

Дата: 07.10.2026. Архитектурный контракт: `ac6352b`; код/тесты: `a809318`. Целевой контракт — [[architecture/CLIENT_SERVER_MODEL]], команды — [[engineering/USAGE#svctl и примеры клиентов]]. Это проверка переноса существующего GUI и нового CLI, а не приёмка будущих multi-client/config/subscription возможностей.

## Изменения

- Существующий Qt GUI перенесён в `examples/sv-client`; бинарник `sv-client`, библиотека и wire сохранены. Кнопки ракурсов/масштаба увеличены до 64 logical px, добавлена панель state; pause/step доступны через CLI.
- `examples/svctl` использует публичную библиотеку без Qt. Разделены parser/request, application loop и main; каждый C++ файл меньше 200 строк. CLI возвращает JSON ACK, ограничивает ожидание и не повторяет мутации после разрыва.
- Installed-package consumer перенесён из `examples/client` в `tests/fixtures/client-consumer`; product examples теперь отделены от fixture.
- GPU/CPU spec включают `svctl`; helper package qualification ожидает новый binary и новый consumer path. Новый RPM payload в этой серии не собирался; старые RPM reports остаются результатом прежних ревизий.
- CMake fingerprint теперь учитывает C++/QML/QRC и CMake файлов клиентских examples после переноса. GTest — системный пакет, включаемый `SV_GTEST_TESTS=ON`; native RPM fixtures от него не зависят.

## Выполненные проверки

| Проверка | Результат |
|---|---|
| Release configure/build, `SV_GTEST_TESTS=ON` | PASS |
| Полный `ctest --test-dir build --output-on-failure` | **14/14**, 36.96 s |
| CPU без Qt/GPU, ASan/UBSan, `SV_GTEST_TESTS=ON` | **10/10**, 3.78 s |
| GTest `svctl_options` | 4 tests: defaults/help, TCP/units, generic parameters, 17 negative argument cases |
| Actual server Unix/TCP/combined и установленный external CMake consumer | PASS; в четырёх профилях CLI проверяет state/preset/orbit/zoom и unknown command, accepted/rejected exit codes |
| Серверный config до/после CLI commands | Bytes unchanged; persistence API ещё отсутствует |
| Qt 6 GUI через реальный сервер | PASS в integration/transport suite |
| Qt 5 сборка перенесённого GUI | PASS (`build-qt5/sv-client`) |
| Qt 5 offscreen `--smoke`, сервер отсутствует | Exit 0; QML загружается. Получение кадров этим отдельным smoke не проверялось |
| `svctl` при отсутствующем Unix endpoint | Exit 3, JSON error `connect: No such file or directory` |
| `ldd build/svctl` | Boost.JSON/container, STL/libc; нет Qt/OpenCV/EGL/GLES |
| Markdown | 68 заметок, 974 локальные ссылки/anchors; 29 PlantUML блоков с delimiters и формулы проверены до добавления этого отчёта. PlantUML не отрисовывался |

Новая сборка до фиксации изменений имела source fingerprint `b26c0682fc7e0212299032f4dff4ba9369c2bd501f7a7dd2d1a8bd3c67d0f529`; исходная Git revision в build metadata — `2d1a283`, поэтому fingerprint и описание изменений нужны для её идентификации. Это не новый performance baseline.

## Что остаётся открытым

На момент проверенной ревизии сервер был односессионным, а `sv-simulator` содержал только контракт будущего Qt 6 приложения. Последующие коммиты добавили базовую Qt control GUI, calibration job commands, ConfigStore persistence для calibration и coarse pipeline timings; они не входят в этот исторический test report. Сервер остаётся односессионным; multi-client, per-session products, полный pipeline trace и физический two-host/Aurora опыт не подтверждены. Protobuf/C++20 не введены; условия пересмотра записаны в архитектуре.
