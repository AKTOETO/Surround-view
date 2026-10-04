# Проверка ПК и будущего устройства Аврора

Связи: [[engineering/BUILD]], [[engineering/AURORA]], [[validation/ACCEPTANCE]], [[prototype/STATUS]]. Первичные текущие отчёты: [[validation/baselines/PC_RTX]], [[validation/baselines/PC_MESA]]. Историческая серия 0.1.0 остаётся в [[prototype/MEASUREMENTS]] и не подменяется новой.

## Контракт опыта

`sv-platform-test` — C++-программа, собираемая тем же кодом для ПК и устройства. Не требует Python, Git, исходного дерева или реальных камер во время запуска. Профиль `surround-view-platform-v1`, JSON schema 1. Создаёт `REPORT.md`, `report.json`, контрольное изображение и marker PNG в **новом** каталоге; существующий каталог отвергается. Markdown содержит полный машинный отчёт в ограждённом JSON-блоке, чтобы его можно было сохранить в Obsidian и впоследствии сравнить.

По умолчанию четыре RGB8-изображения создаются целочисленно в памяти. Их содержимое побайтно воспроизводится независимо от CPU/OpenCV/SIMD. Вход 320×180 для каждой камеры, исходная конфигурация `configs/synthetic.json`; её файловый SHA-256 тоже сохраняется. Этот контрольный набор проверяет реализацию и нагрузку, а не качество фотографической сцены.

Альтернативный `--manifest FILE` читает записанные PNG/JPEG/PPM. Проверяются **все** уникальные файловые SHA-256 из manifest, совпадение calibration IDs и полный первый набор. Между стендами передавать один manifest с теми же байтами изображений; пересоздание фотографического fixture может дать иной хэш из-за декодирования/вычислений и лишить опыт сопоставимости.

## Критерии правильности

| ID | Что проверяется | Порог / ожидаемое состояние |
|---|---|---|
| HASH_SHA256 | Известный эталон `abc` | Точный SHA-256 |
| MATH_OPENCV | Аналитический double против реального OpenCV, четыре камеры, два набора k | 8192 точки; ошибка <1e-9 px, одинаковая валидность |
| MATH_CENTRAL_INVALID | Центральный луч, точка позади и NaN | Центр совпадает; невалидные отвергнуты |
| TRANSFORM_INVERSE | Жёсткое преобразование и обратное | Ошибка <1e-10 m |
| CONFIG_STRICT | Неизвестное поле, неверное вращение, немонотонная модель | Все три отвергнуты |
| MESH_WINDING | Направление треугольников, центр и высота угла | Все площади положительны, контрольные Z совпадают |
| SRGB_LINEAR | Round-trip и смешение в линейном цвете | Ошибка <1e-10 |
| IMAGE_RGB_ORIGIN | PNG, BGR→RGB, асимметричный маркер | Красный верх / синий низ |
| SYNC_COMPLETE_STALE | Полный свежий набор и устаревание | READY → NO_INPUT |
| SYNC_SKEW | Выбор с большим исходным рассогласованием | DEGRADED; итоговый skew не превышает окно |
| SYNC_ORDER_BOUND | Дубликаты, обратный порядок, переполнение | Отказ и ограниченная очередь |
| PROTOCOL_FRAGMENTED | Чтение по байту и двух склеенных сообщений | Один / два правильных пакета |
| PROTOCOL_LIMITS | Framing и десятичный uint64 | Недопустимые длины/значения отвергнуты |
| OPENCV_BOARD | Реальный `findChessboardCornersSB` | 35 внутренних углов; пустой снимок отвергнут |
| OPENCV_INTRINSICS | Реальный `fisheye::calibrate` | Восстановление focal <0.01 px; held-out residual <0.01 px на точных synthetic correspondences |
| FIXTURE_HASHES | Происхождение контрольного набора | Детерминированный hash либо все файлы manifest проверены |
| EGL_GLES_FBO | Контекст, шейдеры, color/depth FBO | Успешное создание GLES 3 |
| GPU_OPENCV_PROJECTION | Float-FBO kernel относительно OpenCV | 8192 точки; ≤0.1 px, ноль validity mismatches |
| CACHED_INPUT_UPLOAD | Поворот виртуального ракурса | Четыре initial uploads, без повторной загрузки |
| OUTPUT_DIMENSIONS | RGBA8 результата | Точное число байт |
| GPU_RGBA_TOP_LEFT | Маркер через весь GPU/readback | Верные каналы, ровно один vertical flip |
| RENDER_VARIANTS | Все нагрузочные варианты | Три повтора каждого варианта, без GL-ошибок |

Численные пороги проверяют согласованность модели; они не определяют допустимую ошибку парковки. При отсутствии backend добавляется `GPU_BACKEND`; аппаратные функции, отсутствующие на устройстве, получают `skip` с причиной. `--require-gpu` превращает недоступный backend в ошибку. Недоступный float diagnostic FBO отдельно даёт skip: работоспособность RGBA8-рендера не доказывает возможность проверки float kernel.

Статус `passed` означает успешность перечисленных критериев, **не полную приёмку всех MUST**. `partial` — есть skip; `failed` — хотя бы одна ошибка. Exit codes: 0 для passed/partial, 2 для failed, 1 для ошибки CLI/конфигурации/записи. При автоматизации дополнительно читать `status`, а не только код 0.

## Время, память и возможности

| Поле | Единицы / область |
|---|---|
| `render_readback_ms` | CPU wall time полного `render`: submission, синхронный RGBA readback, flip/copy, опрос timer |
| `gpu_draw_ms` | GPU query вокруг surface + ego vehicle draw; только valid, без upload/readback |
| `upload_cpu_ms` | CPU wall `glTexImage2D`; не самостоятельное GPU-время копирования |
| `readback_copy_cpu_ms` | CPU wall выделения буферов, чтения, vertical flip; включает ожидание GPU |
| `elapsed_ms` критерия | Один запуск CPU-проверки; диагностическое время, без серии для статистического вывода |
| `peak_rss_mib` | Linux `ru_maxrss` процесса; не VRAM и не суммарная память устройства |
| `graphics` | GL/EGL vendor/version/renderer/extensions, texture/renderbuffer limits, timer availability |
| `build`, `system` | Версии, compiler, source hash, uname, CPU, os-release, память, UTC-время записи |

GPU timer extension `EXT_disjoint_timer_query` [S52] проверяется динамически. При disjoint или неготовом результате значение исключается; причины и количество остаются в отчёте. Если расширения нет, `gpu_draw_ms=null`. GPU-время и readback **не складывать**: CPU-чтение уже включает ожидание. Physical sensor-to-display, IPC/UI latency и dedicated GPU memory пока равны null с объяснением.

Варианты: plane H=0; bowl H=1.5; bowl_dense 64×64; bowl_720p 1280×720; bowl_upload с новыми image owners каждый кадр. Остальные дают 640×360, 32×32 и кэшированные изображения. `triangles` относится к поверхности; 132 треугольника ego-модели указаны отдельно в `graphics` и участвуют в draw во всех вариантах.

По умолчанию 10 warmup + 60 измерений, три новых EGL-контекста **в одном процессе**, прямой/обратный порядок вариантов через повтор. Это не три независимых process runs и не длительный thermal stress. Новая CPU-копия входов для bowl_upload создаётся перед таймером; декодирование и захват отсутствуют в render benchmark. Для каждого повтора сохраняются все raw времена, min/mean/p50/p95/p99/max и population standard deviation. OpenCV принудительно использует один поток; `--opencv-threads N` меняет это явно и влияет на сопоставимость.

## ПК: сформировать базу

```sh
build/sv-platform-test --config configs/synthetic.json \
  --output artifacts/platform-rtx --label PC-RTX5070Ti \
  --egl-platform device --require-gpu
LIBGL_ALWAYS_SOFTWARE=1 build/sv-platform-test --config configs/synthetic.json \
  --output artifacts/platform-mesa --label PC-Mesa \
  --egl-platform surfaceless --require-gpu
```

При нескольких EGL devices выбрать `SV_EGL_DEVICE` после проверки фактического `GL_RENDERER`; имя NVIDIA в системной конфигурации само по себе не доказывает аппаратный рендер. Сохранить исходный report и параметры фоновой нагрузки/энергопрофиля для длительных опытов. Для CPU smoke:

```sh
build/sv-platform-test --config configs/synthetic.json \
  --output artifacts/platform-cpu --label PC-CPU --cpu-only
```

## Устройство и сравнение

После установки RPM на **реальном устройстве**, из пользовательской сессии с доступным EGL:

```sh
sv-platform-test --config /usr/share/surround-view/configs/synthetic.json \
  --output "$HOME/sv-results/run-01" --label Aurora-device \
  --egl-platform default --require-gpu
```

Если устройство поддерживает только отдельный backend, повторить в новом каталоге с подтверждённым `surfaceless` либо `device`; режим EGL намеренно может различаться. При ошибке сохранить failed report, выполнить CPU-only квалификацию и исследовать причину. Эмулятор/QEMU годится для smoke-проверки запуска, его времена нельзя считать аппаратными показателями устройства.

Скопировать результаты на ПК (точные команды и RPM-путь — [[engineering/AURORA]]) и сравнить:

```sh
python3 tools/compare_reports.py docs/validation/baselines/PC_RTX.md \
  artifacts/aurora/run-01/report.json --output artifacts/comparison.md --strict
```

Проверяются suite, config/fixture/source hashes, build type Release, iterations/warmup/repeats/OpenCV threads, область измерения и параметры каждого варианта. Обе квалификации должны быть passed. Различия compiler/OpenCV/CPU/GPU сохраняются в паспортах и допустимы: они являются частью сравниваемой платформы. При несовпадении методики выводятся причины, **отношение времён не вычисляется**, `--strict` возвращает 2. Для совпавшей нагрузки отношение — медиана p95 повторов цели, делённая на медиану p95 базы; оно не равно отношению FPS полного приложения.

Если код изменился после сохранённой базы, заново собрать и измерить ПК с тем же исходным fingerprint, который упакован для устройства. Исторический PC report не редактировать под новые результаты. Таблица характеристик Авроры в дипломе заполняется только после actual-device запуска.

[S52]: https://registry.khronos.org/OpenGL/extensions/EXT/EXT_disjoint_timer_query.txt
