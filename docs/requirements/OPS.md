# Требования к эксплуатации

## Назначение и статус

Редакция от 19.09.2026. Требования к планируемой Linux-сборке, запуску, завершению и воспроизведению. Реализация CLI и измерительные результаты в данном комплекте пока отсутствуют. Решения: [ADR-001/003](../SYSTEM_ARCHITECTURE.md), [план](../MASTER_PLAN.md), [приёмка](../VALIDATION.md). Возможность EGL без оконной поверхности [1] проверяется на конкретном backend; локальные timestamps используют определённую Linux-шкалу [2].

## Требования

| ID | Приоритет | Условие и наблюдаемое поведение | Приёмка | Задача |
|---|---|---|---|---|
| OPS-F-001 | MUST | `sv-server`, `sv-ui`, `sv-simulator`, `sv-bench` имеют отдельные entry points и одно общее ядро. | T-OPS-001: отдельный запуск каждого процесса и инспекция связей. | R5 |
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

## Проектный CLI и его проверка

Следующие команды задают контракт будущей реализации. Сейчас это спецификация, а не проверенная инструкция запуска: исходники, binaries, configs, datasets и wrapper ещё должны быть добавлены на этапах MASTER_PLAN. Вместе с реализацией заменить этот статус результатом T-OPS-002 и указать реальные пути поставленного набора.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/sv-bench --config configs/reference.yaml --dataset data/control/manifest.json --output docs/experiments/math-01
env -u DISPLAY -u WAYLAND_DISPLAY ./build/sv-server --config configs/reference.yaml --source replay --dataset data/control/manifest.json --headless-smoke-test
./build/sv-server --config configs/reference.yaml --source replay --dataset data/control/manifest.json --ipc-dir /tmp/sv-local
./build/sv-ui --ipc-dir /tmp/sv-local
```

Последние два процесса запускаются в разных терминалах. Для producer-режима сервер запускается с `--source producer`, затем выполняется `./build/sv-simulator --dataset data/control/manifest.json --ipc-dir /tmp/sv-local --scenario baseline`. Будущий wrapper `./scripts/run-experiment --profile local-reference --experiment E-PERF-01 --output docs/experiments/perf-01` должен запускать нужные процессы, собирать события и завершать их; до реализации он не считается свидетельством приёмки.

## Паспорт и артефакты

Паспорт и структура каталога определены в [VALIDATION.md](../VALIDATION.md). Большие записи хранятся вне Git; в репозитории — manifest, описание получения, условия использования и контрольные хэши. Единицы времени и clock domain фиксируются для каждого события. Протоколы приёмки хранят наблюдаемые значения, а не только статус команды.

Headless GPU сначала проверяется без графической сессии: доступ к устройству, EGL display/context, FBO, загрузка, рендер, readback, завершение. UI требует собственной графической среды и проверяется отдельным сценарием. Аппаратный адаптер не нужен для математических и протокольных проверок. Ненулевые коды завершения: 2 — конфигурация, 3 — вход/протокол, 4 — GPU, 5 — IPC/ресурс; ожидаемый EOF replay и штатная остановка — 0.

## Источники

Библиографические описания оформлены по ГОСТ Р 7.0.100–2018. Нумерация локальная для этого документа.

1. EGL_KHR_surfaceless_context / Khronos Group. – Текст : электронный // Khronos EGL Registry. – URL: [https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_surfaceless_context.txt](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_surfaceless_context.txt) (дата обращения: 19.09.2026).
2. clock_gettime(3) : Linux manual page. – Текст : электронный // Linux man-pages. – URL: [https://man7.org/linux/man-pages/man3/clock_gettime.3.html](https://man7.org/linux/man-pages/man3/clock_gettime.3.html) (дата обращения: 19.09.2026).
3. ГОСТ Р 7.0.100–2018. Система стандартов по информации, библиотечному и издательскому делу. Библиографическая запись. Библиографическое описание. Общие требования и правила составления : национальный стандарт Российской Федерации : дата введения 2019-07-01. – Москва : Стандартинформ, 2018. – URL: [https://docs.cntd.ru/document/1200161674](https://docs.cntd.ru/document/1200161674) (дата обращения: 19.09.2026). – Текст : электронный.
