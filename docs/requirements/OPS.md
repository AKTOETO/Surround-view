# Требования к эксплуатации

## Ответственность

`OPS` описывает сборку, запуск, остановку, диагностику и воспроизведение системы на Linux.

## Требования

| ID | Приоритет | Требование | Приемка |
|---|---|---|---|
| OPS-F-001 | MUST | Процессы `sv-server`, `sv-ui` и `sv-simulator` должны иметь отдельные entry points. | Каждый запускается собственной командой. |
| OPS-F-002 | MUST | Должен существовать documented full-system launch scenario. | Новый разработчик запускает стенд по README. |
| OPS-F-003 | MUST | Сервер должен поддерживать запуск в foreground для отладки. | Логи видны в terminal. |
| OPS-F-004 | MUST | Сервер должен поддерживать запуск как Linux service. | systemd unit или эквивалент проходит start/stop/restart. |
| OPS-F-005 | MUST | Ошибки запуска должны иметь ненулевой exit code. | Скрипт может обнаружить failure. |
| OPS-F-006 | MUST | Конфигурация запуска должна логироваться вместе с версиями binary, protocol и GPU driver. | Trace содержит metadata эксперимента. |
| OPS-F-007 | MUST | Должны быть отдельные режимы `simulation`, `replay` и `hardware`. | Режим выбирается аргументом/config, а не изменением исходников. |
| OPS-F-008 | MUST | Должен существовать health status каждого input source. | UI/telemetry различает connected, stale, dropped и disconnected. |
| OPS-F-009 | MUST | Остановка процесса должна освобождать transport queues, files и GPU resources. | Повторный запуск успешен без ручной очистки. |
| OPS-F-010 | SHOULD | Должен существовать единый сценарий сбора performance trace. | Эксперимент воспроизводится одной documented-командой. |
| OPS-F-011 | SHOULD | Должен существовать offline replay без подключения simulator. | Запись подается напрямую в server adapters. |

## Ограничения эксплуатации

- Секреты, ключи и реальные CAN credentials не хранятся в Git.
- Большие записи камер хранятся вне репозитория; в Git находятся manifest, schema и контрольные хэши.
- Эксперимент должен фиксировать hardware, OS, driver, configuration, input data и commit ID.
- Production-like hardware adapter не должен требоваться для unit-тестов геометрии и протокола.
