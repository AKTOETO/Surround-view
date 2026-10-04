# Сборка, зависимости и установка

Версия кода — 0.2.0. Начало работы: эта заметка → [[engineering/USAGE]] → [[engineering/PLATFORM_TEST]]. Для устройства использовать [[engineering/AURORA]], для происхождения демонстрации — [[engineering/SCENE]]. Фактические ограничения находятся в [[prototype/STATUS]].

## Требования к среде

| Компонент | Требование | Проверенная Linux-среда | Назначение |
|---|---|---|---|
| Компилятор | C++17 | GCC 16.2 | Все исполняемые инструменты |
| CMake / Ninja | ≥3.20 / доступный Ninja | Системные пакеты | Сборка и установка |
| Boost | ≥1.75, JSON и заголовки Asio | 1.92 | JSON, framing, асинхронные сокеты |
| GLM | CMake package `glm` | 1.0.3 | Матрицы виртуальной камеры |
| OpenCV | ≥4.5 | **5.0.0** | Реальная проекция, изображения, детектор, калибровка, VideoCapture |
| OpenSSL | Crypto development package | 3.6.4 | SHA-256 исходников и входных данных |
| EGL / GLES | EGL и OpenGL ES 3 | NVIDIA / Mesa | GPU-профиль |
| Qt | 5.15 либо 6, Core/Gui/Qml/Quick/Network | 6.11.2 | Desktop-клиент, опционально |
| Python | Python 3, NumPy; Matplotlib для рисунков | 3.14.7 / 2.5.3 | Host-тесты, аналитический генератор, известные XYZ, графики |

Python **не нужен на устройстве** для `sv-platform-test`, `sv-calibrate`, `sv-capture` и `sv-scene`. Сравнение полученных отчётов выполняется на ПК стандартным Python без NumPy.

В OpenCV 4 требуются `core`, `calib3d`, `imgproc`, `imgcodecs`, `videoio`. В OpenCV 5 CMake выбирает `geometry`, `calib`, `objdetect` вместо прежнего монолитного `calib3d`; исходники учитывают перенос детектора. Проверен OpenCV 5; совместимость с веткой 4 заложена в сборке, но требует проверки на соответствующем SDK.

**CMake ничего не скачивает.** Все библиотеки ищутся через `find_package`/`pkg-config` с `REQUIRED`; отсутствие зависимости останавливает конфигурацию. `FetchContent` отсутствует и в Linux-, и в Aurora-профиле. Установить development-пакеты в выбранную среду до конфигурации. Название `opencv5` в `pkg-config` этого ПК отличается от часто встречающегося `opencv4`; проект использует CMake-конфигурацию OpenCV, а не угадывает имя `.pc`.

```sh
cmake --version
pkg-config --modversion egl glesv2 openssl
pkg-config --modversion opencv5
```

Последняя команда — диагностика именно проверенного ПК. Главная проверка наличия всех зависимостей — успешная конфигурация CMake ниже.

## Полный Linux-профиль

Команды из корня исходников:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSV_GPU=ON -DSV_CLIENT=ON -DSV_QT_MAJOR=6
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

При установленном Qt 5 заменить `SV_QT_MAJOR=6` на `5`. Если клиент не нужен, задать `SV_CLIENT=OFF`; запрошенный, но отсутствующий Qt считается ошибкой. Интеграционный тест использует программный Mesa и offscreen-клиент, проверяет replay, управление, release и reconnect; нужны локальные Unix-сокеты. Проверка `vision_tools` использует записанные MJPEG-файлы и проецированные доски, не требует подключённых камер.

## CPU и санитайзеры

```sh
cmake -S . -B build-cpu -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DSV_GPU=OFF -DSV_CLIENT=OFF -DSV_SANITIZERS=ON
cmake --build build-cpu --parallel 4
ctest --test-dir build-cpu --output-on-failure
```

Этот профиль оставляет OpenCV/OpenSSL и все native camera/report-инструменты. Он исключает EGL и Qt. Времена Debug/ASan не сравниваются с Release. Если среда запуска ограничивает `ptrace`, LeakSanitizer или `bind`, повторять соответствующую проверку в обычной локальной среде; не менять математические пороги для получения успешного статуса.

## Параметры CMake

| Параметр | По умолчанию | Поведение |
|---|---|---|
| `SV_GPU` | ON | Renderer, benchmark, server и GPU-проверки |
| `SV_CLIENT` | ON | Qt Quick desktop-клиент |
| `SV_QT_MAJOR` | Автовыбор | Принудительно 5 или 6 |
| `SV_PLATFORM_TEST` | ON | Native отчёт и `sv-core-tests` для устройства |
| `SV_AURORA` | OFF | Offline SDK-профиль, отключает host Python-тесты |
| `SV_PYTHON_TESTS` | ON | Регистрация Python-тестов; принудительно OFF при cross-compiling |
| `SV_SANITIZERS` | OFF | ASan/UBSan для исследования CPU-кода |
| `BUILD_TESTING` | ON | Регистрация CTest; RPM собирается с OFF |

Устройства и архитектуру задаёт toolchain SDK, а не `SV_AURORA`: этот флаг сам по себе не превращает x86_64-бинарник в aarch64.

## Установка без исходного дерева

```sh
cmake --install build --prefix "$PWD/artifacts/install"
artifacts/install/bin/sv-platform-test \
  --config artifacts/install/share/surround-view/configs/synthetic.json \
  --output artifacts/installed-report --cpu-only --label installed-linux
```

Для RPM `CMAKE_INSTALL_PREFIX=/usr`, staging через `DESTDIR`. Бинарники находятся в `bin/`, конфигурации и фотоисточник — в `share/surround-view/`, инструкции — в его `docs/`. GLSL включён в бинарник через CMake-generated header, QML — через Qt resource `.qrc`. Запуск не читает shader/QML из checkout. Выходные данные всегда направлять в доступный для записи пользовательский каталог, а не в `/usr/share`.

## Структура кода и стиль

| Каталог | Ответственность |
|---|---|
| `src/core`, `include/sv` | Контракты конфигурации, независимая аналитическая математика, синхронизация, framing |
| `src/vision` | OpenCV: проекция, decoding, шаблон, fitting и проверочная репроекция |
| `src/render` | EGL/GLES, surface/vehicle shaders, GPU timers и финальный readback |
| `src/validation` | Native критерии, статистика, SHA-256, системный паспорт и Markdown-отчёт |
| `src/apps` | Отдельные CLI-программы |
| `src/client` | Qt/QML-мост и упакованные ресурсы |
| `tools`, `tests` | Host-оркестрация, генераторы и независимые проверки |
| `packaging/rpm` | Два spec-профиля: GPU и CPU |

`.clang-format` основан на **Microsoft**, с Allman-скобками, отдельными определениями функций, запретом коротких однострочных ветвей и шириной 100. Запустить `clang-format -i` для изменённых `.cpp`/`.hpp`; шаблоны `.hpp.in` с GLSL так форматировать не следует. Сборочный отчёт хранит SHA-256 содержимого C++/GLSL/QML/CMake: различие реализации обнаруживается даже при одинаковом Git HEAD и незакоммиченных изменениях.
