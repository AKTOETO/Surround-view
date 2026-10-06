# sv-client: автомобильный GUI

Пример использования `sv-client-lib`: готовый кадр, single-touch drag для orbit, крупные кнопки ракурсов и масштаба, панель текущего state. Replay pause/step и инженерные настройки вынесены из главного экрана; существующий API доступен через `svctl`.

```sh
cmake -S . -B build-qt5 -DSV_QT_MAJOR=5 -DSV_CLIENT=ON
cmake --build build-qt5 --target sv-client
build-qt5/sv-client /tmp/sv-prototype
build-qt5/sv-client --tcp 127.0.0.1 53101 53102
```

Команды выполняются из корня checkout. Существующий клиент поддерживает сборку Qt 5/6; целевой профиль — Qt 5 под Авророй. Это Qt Quick host application: AuroraApp/Silica, DPI/физические размеры touch targets, реальные экран и жесты ещё требуют адаптации и проверки. Значение кнопки 64 — логические пиксели, не гарантия размера в миллиметрах. Библиотека и Bridge владеют кадрами и освобождают их по текущему протоколу.
