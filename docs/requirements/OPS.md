# Требования к эксплуатации

## Назначение и статус

Рабочая редакция от 04.10.2026; основа от 19.09.2026. Требования к планируемой Linux-сборке, запуску, завершению и воспроизведению. Реализация CLI и измерительные результаты в данном комплекте пока отсутствуют. Решения: [[architecture/DECISIONS|ADR-001/003]], [план](../planning/ROADMAP.md), [приёмка](../validation/ACCEPTANCE.md). Возможность EGL без оконной поверхности [[references/DEVELOPMENT#S46|S46]] проверяется на конкретном backend; локальные timestamps используют определённую Linux-шкалу [[references/MEASUREMENT#S48|S48]].

## Требования

| ID | Приоритет | Условие и наблюдаемое поведение | Приёмка | Задача |
|---|---|---|---|---|
| OPS-F-001 | MUST | `sv-server`, `sv-client`, `sv-simulator`, `sv-bench` имеют отдельные entry points и одно общее ядро. | T-OPS-001: отдельный запуск каждого процесса и инспекция связей. | R5 |
| OPS-F-002 | MUST | Документирован полный локальный запуск replay → server → Qt Quick (QML), producer выбирается альтернативно. | T-OPS-002: повтор другим разработчиком по поставленной инструкции. | R5 |
| OPS-F-003 | MUST | Сервер работает в foreground с диагностикой и может запускаться без UI. | T-OPS-003: логи терминала, команды stop и T-SRV-001. | R5 |
| OPS-F-004 | SHOULD | После headless-проверки оформляется systemd-unit. | T-OPS-004: start/stop/restart на принятой платформе. | R5 |
| OPS-F-005 | MUST | Ошибки запуска возвращают ненулевой exit code с категорией причины. | T-OPS-005: config/input/GPU/IPC failure различаются. | R5 |
| OPS-F-006 | MUST | Каждый опыт сохраняет эффективную конфигурацию, commit/dirty state, версии, паспорт GPU/API и хэши данных. | T-OPS-006: полный manifest запуска и повтор одного опыта. | R6 |
| OPS-F-007 | MUST | Режимы replay и producer выбираются аргументом/конфигурацией, алгоритм не меняется. | T-OPS-007: переключение между запусками; hardware — SHOULD при наличии адаптера. | R5 |
| OPS-F-008 | MUST | Health содержит состояния процесса/камер, возраст, skew и счётчики причин потерь. | T-OPS-008: stale как состояние, dropped как счётчик; переходы SRV-F-011. | R5 |
| OPS-F-009 | MUST | Остановка освобождает файлы, IPC и GPU-ресурсы в ограниченный срок; новый запуск восстанавливает сессию. | T-OPS-009: SIGTERM, потеря клиента и restart без ручной очистки; срок в паспорте. | R5 |
| OPS-F-010 | MUST | Один документированный сценарий объединяет trace ядра, сервера и UI и создаёт каталог опыта. | T-OPS-010: полнота метаданных/событий, потери trace отражены; E-PERF-01. | R6 |
| OPS-F-011 | MUST | Сервер воспроизводит готовый набор без simulator: play, pause, step и повтор с новым runtime mapping. | T-OPS-011: исходные timestamps сохранены, время исполнения актуально, PAUSED явно виден. | R5, R6 |
| OPS-F-012 | MUST | Для целевого устройства сохраняются сборка, зависимости, пакетирование, установка и воспроизводимый полный запуск. | T-OPS-012: E-TARGET-01, паспорт Авроры и фактические команды; desktop CLI не считается инструкцией для устройства. | R5, R6 |

## Проектный desktop CLI и его проверка

Следующие команды задают контракт будущей реализации. Сейчас это спецификация, а не проверенная инструкция запуска: исходники, binaries, configs, datasets и wrapper ещё должны быть добавлены на этапах MASTER_PLAN. Вместе с реализацией заменить этот статус результатом T-OPS-002 и указать реальные пути поставленного набора.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/sv-bench --config configs/reference.yaml --dataset data/control/manifest.json --output artifacts/experiments/math-01
env -u DISPLAY -u WAYLAND_DISPLAY ./build/sv-server --config configs/reference.yaml --source replay --dataset data/control/manifest.json --headless-smoke-test
./build/sv-server --config configs/reference.yaml --source replay --dataset data/control/manifest.json --ipc-dir /tmp/sv-local
./build/sv-client --ipc-dir /tmp/sv-local
```

Последние два процесса запускаются в разных терминалах. Для producer-режима сервер запускается с `--source producer`, затем выполняется `./build/sv-simulator --dataset data/control/manifest.json --ipc-dir /tmp/sv-local --scenario baseline`. Будущий wrapper `./scripts/run-experiment --profile local-reference --experiment E-PERF-01 --output artifacts/experiments/perf-01` должен запускать нужные процессы, собирать события и завершать их; до реализации он не считается свидетельством приёмки.

## Паспорт и артефакты

Паспорт и структура каталога определены в [ACCEPTANCE.md](../validation/ACCEPTANCE.md). Большие записи хранятся вне Git; в репозитории — manifest, описание получения, условия использования и контрольные хэши. Единицы времени и clock domain фиксируются для каждого события. Протоколы приёмки хранят наблюдаемые значения, а не только статус команды.

Headless GPU сначала проверяется без графической сессии: доступ к устройству, EGL display/context, FBO, загрузка, рендер, readback, завершение. UI требует собственной графической среды и проверяется отдельным сценарием. Аппаратный адаптер не нужен для математических и протокольных проверок. Ненулевые коды завершения: 2 — конфигурация, 3 — вход/протокол, 4 — GPU, 5 — IPC/ресурс; ожидаемый EOF replay и штатная остановка — 0.


## Связанные источники

[[references/README|Единый каталог литературы и документации]].
