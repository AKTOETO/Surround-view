# Сквозная демонстрация Linux-прототипа

29.10.2026. Проверен новый low/street-a/mount1001 Blender replay из E2/E3, а не
шахматный генератор. Native server рендерит dome_floor+edge_feather, svctl и оба Qt GUI
используют sv-client-lib. Сессии последовательные: multi-client приёмка не заявляется.

## Результат

- CLI state/orbit/state: команды accepted, azimuth0.8→1.7rad,elevation0.35→0.85rad.
  `orbit` принимает **приращения**, а не абсолютные координаты. Config revision0
  сохраняется; runtime view command не переписывает исходный JSON.
- Автомобильный пример sv-client получил64 frames и63 ui_present_submit events.
  Каждый presented frame ранее получен; offscreen software Qt не подтверждает физическое
  отображение пикселя и не измеряет sensor-to-display.
- Ноутбучный sv-simulator --smoke получил frame и завершился0, без QML warnings в log.
- Server завершён штатно, trace заканчивается shutdown. Actual renderer RTX5070Ti.

Build после GUI-only исправления strict TCP port parsing имеет fingerprint
d2e0d802f324ff66d0542ed38ad974bdbc57f6d0a57cce6d5e30231db4442fd1.
E2/performance выполнены раньше на a3ae77…; их reports не переписывались. Полная Linux
сборка и CTest70/70 за194,94с успешны, включая18 malformed-port subprocess checks,
32 scientific fixture tests, real server/library/Qt transports и camera MOCK.

Raw artifacts/thesis-demo-20261029: исходные4 PPM/config/manifest, ACKs, client/simulator
logs и server trace. Compact audit:
[thesis_demo_v1.json](baselines/thesis_demo_v1.json). Этой демонстрацией проверяется
работающий protocol/frame path; это один static recording, не физика вождения.

## Запустить с интерфейсом

При сохранённых artifacts исследования конфигурация уже подготовлена. В первом терминале:

```sh
build/sv-server --config artifacts/thesis-demo-20261029/config.json \
  --manifest artifacts/thesis-demo-20261029/manifest.json --loop true \
  --ipc-dir /tmp/sv-demo --trace artifacts/demo-interactive-trace.jsonl
```

В другом терминале запустить один клиент:

```sh
build/examples/sv-simulator/sv-simulator --unix /tmp/sv-demo
# Либо автомобильный пример:
build/sv-client /tmp/sv-demo
# Либо команды через библиотеку:
build/svctl --unix /tmp/sv-demo orbit 0.1 0.1
build/svctl --unix /tmp/sv-demo state
```

Перед переключением закрыть предыдущий GUI. Для automated offscreen smoke вместо
интерактивного просмотра задать QT_QPA_PLATFORM=offscreen,QT_QUICK_BACKEND=software и
--smoke. Сервер останавливается Ctrl+C.

В режиме loop один сохранённый кадр повторяется для устойчивой демонстрации. Он не
становится новой scene/time observation. Чтобы показать движение, нужен отдельный
многокадровый export, а Qt Quick 3D driving preview пока не является live producer
камер для этого сервера.

Для переноса записи достаточно config.json,manifest.json и4 PPM с теми же относительными
именами; Blender/Python на устройстве не нужны. В установленном RPM заменяются пути
к binaries, а данные копируются в доступный пользователю каталог. Это инструкция
переноса формата, не свидетельство уже выполненного запуска Авроры.

## Что проверено без физических камер

Full suite включает camera config/negotiation/readback, false/empty/exception open/read,
retry/backoff/budget, queue/provenance и stop на границах mocked calls. Контракты и
тестовые seam: [[engineering/SOURCES]], [[engineering/SOURCE_LIFECYCLE]]. Повторять эту
работу как новый исследовательский sweep не нужно. Остаются собственно аппаратные
driver calls, bounded cancellation зависшего read/open/release, physical timestamps и
disconnect/reconnect. MOCK не доказывает эти свойства реального /dev/video*.

Aurora SDK/mb2/sdk-assistant в текущем окружении отсутствуют. Поэтому target RPM,
Aurora Qt5/Silica lifecycle, rpm-validator и установка на автомобильном устройстве
остаются отдельной приёмкой. Linux host RPM проверяет payload/consumer, но не заменяет
целевые ABI/permissions. Исходные данные и этапы: [[engineering/AURORA]],
[[engineering/PLATFORM_TEST]], [[validation/ACCEPTANCE]].

30.10.2026 дополнительно собраны и проверены CPU/GPU Linux RPM из чистой9168acb:
[[validation/SOURCES_SMOKE#Текущая проверка CPU/GPU RPM 30.10.2026]]. Они содержат server/
library/tools, GUI выключен default spec; это не пакетная приёмка автомобильного UI.
