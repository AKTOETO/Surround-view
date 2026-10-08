# svctl: настройка и управление без Qt

CLI использует только публичную `sv-client-lib`, Boost и STL. Поддерживаются существующие `state`, `pause`, `resume`, `step`, `preset`, `orbit`, `zoom`; неизвестное расширение передаётся через `command TYPE --params JSON_OBJECT`, а сервер решает, поддерживает ли его. Новые config/subscription операции пока отсутствуют на сервере; CLI не имитирует их редактированием файла.

```sh
cmake -S . -B build -DSV_CLIENT=OFF
cmake --build build --target svctl
build/svctl --unix /tmp/sv-prototype state
build/svctl --tcp 127.0.0.1 53101 53102 preset top
build/svctl --unix /tmp/sv-prototype orbit 0.1 -0.05
build/svctl --unix /tmp/sv-prototype zoom -0.5
build/svctl --unix /tmp/sv-prototype pause
build/svctl --unix /tmp/sv-prototype step
build/svctl --unix /tmp/sv-prototype resume
build/svctl --unix /tmp/sv-prototype cancel-calibration calib-job-42
build/svctl --unix /tmp/sv-prototype command state --params '{}'
```

Вывод — один JSON ACK на stdout. Exit codes: 0 accepted, 2 неверные аргументы, 3 соединение/протокол/таймаут, 4 сервер отклонил команду. `--timeout-ms 5000` ограничивает полное ожидание connect+command; допустимо 10…60000 ms. Автоматического повторения мутации после разрыва нет. При timeout после отправки результат операции может быть неизвестен.

Пока клиентская библиотека всегда открывает control/data: CLI освобождает поступающие RGBA и ждёт ACK. Настоящая control-only session и отключение final output — следующий этап. Сервер пока принимает одного клиента; одновременно работающий GUI может занимать сессию. Библиотека имеет bounded event queue, CLI не ждёт получения видеокадра для успешного завершения команды.

Unit tests на системном GTest включаются явно, без скачивания:

```sh
cmake -S . -B build -DSV_GTEST_TESTS=ON
cmake --build build --target svctl-option-tests
ctest --test-dir build -R svctl_options --output-on-failure
```
