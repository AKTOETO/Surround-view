# Linux-прототип: сборка и запуск

Рабочая версия от 05.10.2026. Реализованы C++17-ядро, EGL/OpenGL ES 3-рендерер, replay-сервер с Boost.Asio, desktop-клиент Qt Quick, Python-генератор данных и offline-калибровка по известным точкам. Состояние требований и ограничения: [[prototype/STATUS]]. Подробное описание реализации: [[diploma/03_PROTOTYPE_IMPLEMENTATION]].

## Зависимости и структура

CMake ≥3.20, C++17, Boost.JSON/Boost.Asio, GLM, EGL/GLESv2. Для графического клиента — Qt 5.15 или Qt 6 с Core/Gui/Qml/Quick/Network. Python-инструменты используют NumPy, графики — Matplotlib. В проверенной среде есть Boost 1.92, GLM 1.0.3, Qt 6.11.2; OpenCV не используется. GLM сначала ищется в системе, иначе CMake получает версию 1.0.3 через FetchContent. Эта загрузка требует сети только при отсутствии системной библиотеки.

| Каталог | Ответственность |
|---|---|
| `include/sv/`, `src/core/` | Математика, валидация JSON, изображения, синхронизация, framing |
| `src/render/` | EGL-ресурсы и GLSL ES shaders |
| `src/apps/` | `sv-project`, `sv-bench`, `sv-server` |
| `src/client/` | C++-мост, QML-интерфейс, получение изображения |
| `tools/` | Данные, калибровка, CLI-клиент, серия измерений |
| `tests/` | C++-проверки, независимый NumPy-эталон, калибровка и интеграция |
| `configs/` | Версионированные небольшие входные конфигурации |
| `artifacts/` | Генерируемые записи, отчёты и трассы; исключены из Git |

`sv-core` не зависит от Qt или Asio. Boost.JSON и GLM — зависимости вычислительного профиля; разрешённость этих библиотек для Авроры ещё не проверена. Сервер владеет одним EGL-контекстом в основном потоке; отдельный поток `io_context` обслуживает сокеты. Общий пул работников пока не требуется.

## Сборка и тесты

Команды выполняются из корня репозитория.

```sh
cmake -S . -B build -G Ninja -DSV_GPU=ON -DSV_CLIENT=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

GPU-тест интеграции принудительно использует Mesa, локальные Unix-сокеты и offscreen-клиент. В ограниченной среде запрет `bind` может мешать именно интеграции; в проведённой проверке она запускалась вне песочницы. GUI-тест проверяет получение и передачу кадра на представление, без физического дисплея. Если Qt отсутствует, клиент отключается, а соответствующая часть теста пропускается; это видно в отчёте.

Отдельная сборка CPU с ASan/UBSan:

```sh
cmake -S . -B build-cpu -G Ninja -DSV_GPU=OFF -DSV_CLIENT=OFF -DSV_SANITIZERS=ON
cmake --build build-cpu -j 4
ctest --test-dir build-cpu --output-on-failure
```

LeakSanitizer не работает под ptrace используемой песочницы; эта сборка проверена вне неё. Проверки не требуют скачивания тестового framework.

## Данные и первый кадр

```sh
python3 tools/simulator.py --config configs/synthetic.json --output artifacts/fixture --frames 12
build/sv-bench --config configs/synthetic.json --manifest artifacts/fixture/manifest.json --output artifacts/mesa-bowl --warmup 10 --iterations 60
```

Для принудительного программного профиля задать `LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless`. Для аппаратного NVIDIA-профиля:

```sh
build/sv-bench --egl-platform device --config configs/synthetic.json --manifest artifacts/fixture/manifest.json --output artifacts/rtx-bowl --warmup 10 --iterations 60
```

`SV_EGL_DEVICE` выбирает индекс EGL-устройства при нескольких доступных устройствах. `metrics.json` всегда содержит `gl_vendor` и `gl_renderer`; наличие RTX в `nvidia-smi` не заменяет эти поля. GPU должен быть доступен процессу; в проведённой среде NVIDIA-опыты запускались вне песочницы.

В выходном каталоге: `effective_config.json`, `projection.json`, `metrics.json`, `preview.ppm`. Все измерения времени benchmark включают финальный синхронный readback; чистое GPU-время не объявлено измеренным. Изображения входа RGB8, результат RGBA8, первая строка сверху. Преобразование YUV и аппаратный capture не поддерживаются.

## Сервер и интерактивный клиент

Выбрать свободный каталог сокетов. Сервер не удаляет ранее существовавшие файлы по этим путям.

```sh
build/sv-server --config configs/synthetic.json --manifest artifacts/fixture/manifest.json --ipc-dir /tmp/sv-prototype --trace artifacts/server_trace.jsonl
```

В другом терминале:

```sh
build/sv-client /tmp/sv-prototype
```

Перетаскивание меняет ракурс, колесо — расстояние; доступны три пресета, pause/resume/step. `Ctrl+C` завершает сервер и удаляет принадлежащие ему сокеты. Клиент пытается восстановить соединение. Текстовый клиент для одного кадра:

```sh
python3 tools/client.py --ipc-dir /tmp/sv-prototype --preset top --output artifacts/capture
```

Сервер читает PPM-запись напрямую. Отдельный producer по сети не реализован. Python-клиент подтверждает освобождение серверного кадра после получения его копии; Qt-клиент — после создания собственной копии QImage. В текущем профиле один кадр ожидает release, дедлайн 250 мс. Отсутствие release закрывает видеосоединение, не блокируя control-сокет.

## Калибровка и диагностика

`tools/configurator.py` получает измеренные соответствия: точки в автомобильной системе и пиксели каждой камеры. Формат наблюдений — `schema_version: 1`, `cameras` с `id`, `train` и `validation`; каждая выборка содержит массивы `points` (N×3) и `pixels` (N×2). Пример наблюдений создаёт серия опытов ниже.

```sh
python3 tools/configurator.py calibrate --config configs/synthetic.json --observations artifacts/experiments-rtx/observations.json --output artifacts/calibration-new
python3 tools/configurator.py diagnose --config artifacts/calibration-new/calibrated.json --observations artifacts/experiments-rtx/observations.json --output artifacts/diagnostic
```

Каждая выборка требует ≥30 известных точек с неплоской совокупной геометрией. Реальные изображения и детектор углов нужно добавить отдельно. Экспорт допустим только после проверки отложенных точек и монотонности модели; существующий `calibrated.json` не перезаписывается. `report.json` хранит ошибки до/после, обусловленность, метод и хэш наблюдений. Для replay с новой калибровкой необходимо подготовить совместимые calibration IDs в manifest; сервер отвергает несовпадение.

## Повторение опытов и построение графиков

```sh
python3 tools/run_experiments.py --platform surfaceless --output artifacts/experiments-mesa --repeats 3 --iterations 60
python3 tools/run_experiments.py --platform device --output artifacts/experiments-rtx --repeats 3 --iterations 60
python3 docs/diploma/plot_experiments.py --previews artifacts/experiments-rtx
```

Скрипт главных графиков по умолчанию читает сохранённую серию из [[prototype/MEASUREMENTS]], что позволяет воспроизвести рисунки без GPU. Новый опыт сначала сохраняется в `artifacts/`; его результаты переносятся в документацию как отдельная версия, с сохранением прежних данных и обновлением текста. Выводы старого запуска нельзя молча заменять новым изображением.
