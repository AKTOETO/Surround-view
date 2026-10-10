# Проверка библиотеки, listeners и RPM 0.5.0

Дата: 06.10.2026. Реализация: `506e44e`, документация: `6d85024`, исправление RPM layout: `876c117`. Каталог API/команды: [[engineering/CLIENT_LIBRARY]], полный аудит: [[planning/AUDIT]].

## Результаты

### Дополнение 11.10.2026: проверка release до очереди и на сервере

Исправлены две ошибки frame-release пути. Публичный Client::release проверял типы полей, но не ограничение размера до post: на активном соединении oversized metadata могли вызвать необработанное исключение encode в network worker. Теперь encode validation выполняется синхронно до расходования release submission budget. Native GTest до исправления воспроизвёл отсутствие требуемого exception до ready; после исправления проверяется также активная сессия с насыщенной command queue: oversized release отклоняется, правильный release сразу занимает доступный собственный слот и доставляется серверу.

Сервер прежде принимал type 22 с произвольным binary body и правильным token. Новый integration case до исправления получил следующий кадр вместо закрытия data connection. Теперь payload обязан быть пустым; ошибочное сообщение закрывает data, не выполняя перехода release. Control state request после data EOF всё ещё принимается, процесс сервера жив. Для штатной библиотеки такой EOF запускает её обычное завершение обоих каналов/reconnect; это не новый wire error или ACK.

Проверки: два GTest cases в `client_release_budget`, malformed release в `replay_ipc`. Полная сборка сервера/библиотеки/GUI успешна; `ctest --test-dir build --output-on-failure -j4` — **50/50**, 25.97 s. Это проверки SV01/очередей, не длительный two-host overload или финальная оценка производительности.

### Дополнение 10.10.2026: порядок конкурентных команд

В `sv-client-lib` исправлена гонка между выдачей command ID и post в Asio worker. Atomic increment сам по себе не сохранял wire order: native TCP regression из восьми потоков воспроизвёл начало `3,4,5,1,2` и rejects `duplicate_or_out_of_order`. Участок allocation/encode/post теперь сериализован mutex; release использует прежнюю независимую очередь. Wrap uint64 запрещён guard-ом, прямой exhaustion test пока отсутствует.

Новый GTest/CTest `client_command_order` отправляет 256 команд с разной стоимостью encoding, сохраняет coalesced TCP messages, проверяет возрастающие IDs и все accepted ACK. До изменения production кода тест упал; после исправления прошёл. Полная сборка сервера, библиотеки и обоих GUI успешна; `ctest --test-dir build --output-on-failure -j4` — **49/49**, 20.70 s. Это localhost library regression, не multi-client или межмашинная проверка. Полный текущий каталог сценариев: [[engineering/PROTOCOL_SCENARIOS]].

### Исторический стенд 0.5.0

| Проверка | Результат | Ограничение |
|---|---|---|
| Release CTest | 10/10 групп | Native lifecycle + настоящие Unix/TCP/GUI integration tests |
| CPU Debug ASan/UBSan | 7/7 групп | Библиотека/CPU, без GPU-сервера и Qt |
| Unix-only policy | Нет IP sockets процесса в `/proc` | Проверены IPv4/IPv6 TCP/UDP таблицы на Linux |
| TCP-only/combined | ACK, RGBA и shutdown пройдены | Один клиент, localhost |
| External installed consumer | CMake package + headers/static libraries работают | ldd без Qt/OpenCV/EGL/GLES |
| Restart/reconnect | Кадры двух различных сессий | Loopback; two-host испытание открыто |
| Pause/orbit/step | Нет повторного decode/upload/mesh build при orbit; один step продвигает набор | File replay |
| 500 команд на паузе | Приняты все команды в пакетах по 16; revision +500, decode/upload/mesh неизменны | Server control stress, не полный library overload/latency acceptance |
| Irregular manifest | Replay сохраняет scenario intervals вместо 30 FPS | ОС вносит scheduling jitter |
| GPU Linux RPM | 25 pass, 0 fail, 0 skip; core 4684 checks | SDK/ABI/signing Авроры не проверены |
| CPU Linux RPM | 17 pass, 0 fail, 1 ожидаемый skip GPU; core 4684 checks | CPU-only partial native qualification |
| Development part из обоих RPM | Внешний consumer успешно собран; dependency isolation пройдена | Проверка Linux development payload |

Host suites: `tests/client_lifecycle.cpp`, `tests/test_client_transport.py`, `tests/test_integration.py`. Команды: `ctest --test-dir build --output-on-failure`, аналогично `build-cpu`. Вторая группа содержит семь host test cases, включая unpacked CMake install consumer. Первоначальные failures lib/lib64 и discovery lib64 выявлены verifier и исправлены; конечный результат ниже относится к повторной проверке готовых пакетов.

## Происхождение RPM

Оба Source0 созданы из revision `876c117a36a4bcae97e71743a5b106d462d4aa8d`; бинарные исходники совпадают с C++ implementation, использованной в тестах. Inspector дополнительно получил discovery через явный svClient_DIR, поскольку host CMake не искал RPM lib64 автоматически. Эта поправка verifier не меняет payload.

- `artifacts/rpm-v05-gpu-final/RPMS/x86_64/surround-view-0.5.0-1.x86_64.rpm`: SHA-256 `cb7b4396bd0f487ec40828ad03c61784ed9f94ca743de1542e16b6b0b78d85d8`.
- Source0 SHA-256: `344bc724d80096851d6776fb3f0a4daed0cd94c2a7a7f8c24ce0d567e1ae1aa2`.
- `artifacts/rpm-v05-gpu-final/INSTALLED_REPORT.md`, `CLIENT_CONSUMER.md`, `files.txt`, `requires.txt`, `build.log` сохраняют первичные результаты.
- `artifacts/rpm-v05-cpu-final/RPMS/x86_64/surround-view-cpu-0.5.0-1.x86_64.rpm`: SHA-256 `202b55fe78dda994f6471dff1a9c867eccf30833c99355d77974c2b7e3a1307a`.
- Source0 SHA-256: `344bc724d80096851d6776fb3f0a4daed0cd94c2a7a7f8c24ce0d567e1ae1aa2`.
- `artifacts/rpm-v05-cpu-final/INSTALLED_REPORT.md`, `CLIENT_CONSUMER.md`, `files.txt`, `requires.txt`, `build.log` сохраняют первичные результаты.

Бинарные RPM/архивы/логи относятся к artifacts и не добавлены в Git. Чистый HEAD, команды verifier и source hash позволяют повторить сборку; outputs должны быть новыми.

## Полные PC baselines текущей реализации

Оба прогона: build revision `6d8502450a6f480d06d5297f1d27151fe8cae4cb`, implementation SHA-256 `cbab94f8ca2c19c70ae2c40fa10ff4f95dfadcc193d4e1e8d635a8c93035b0ae`. 60 measurements + 10 warmup, три новых EGL contexts в одном процессе, OpenCV threads=1; десять workload. Прогоны RTX и Mesa выполнены последовательно, без параллельной нагрузки другого benchmark. Фоновая desktop-сессия сохранялась; это не длительный thermal stress.

RTX: `NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2`; Mesa: `llvmpipe (LLVM 22.1.8, 256 bits)`. По 25 критериев passed, strict workload comparison пройден. Первичные Markdown со всеми raw данными: [[validation/baselines/PC_RTX_V05]], [[validation/baselines/PC_MESA_V05]]. Сравнение: [[validation/PC_COMPARISON_V05]]. Исторические PC_RTX/PC_MESA сохранены без изменений.

| Workload | RTX median p95 ms | Mesa median p95 ms |
|---|---:|---:|
| bowl | 0.16982 | 0.89906 |
| bowl_720p | 0.98448 | 2.57899 |
| bowl_dense | 0.16307 | 1.53959 |
| bowl_upload | 0.28369 | 1.33185 |
| cube_floor | 0.15893 | 0.93366 |
| cylinder_floor | 0.16625 | 1.60605 |
| dome_angular | 0.15545 | 1.72185 |
| dome_floor | 0.19516 | 1.60945 |
| dome_hard | 0.15432 | 1.44775 |
| plane | 0.16510 | 1.36788 |

p95 относится к render/readback CPU wall, не к FPS приложения или physical display latency. GPU draw/raw и области таймеров сохранены в первичных отчётах. Full settings не превращают эту короткую серию в thermal/VRAM/network latency acceptance.

## Открытые условия

Two-host TCP, UDP, live FrameSource/producer, распределённые часы, runtime конфигуратор/calibration API, device SDK и hardware ещё открыты. Проверенные части checklist отмечены отдельно; все оставшиеся работы перечислены в [[planning/AUDIT]].
