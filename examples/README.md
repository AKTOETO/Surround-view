# Примеры клиентов sv-server

Все пользовательские C++ клиенты используют `sv-client-lib` (`sv::client`). Они не редактируют серверную конфигурацию напрямую. Целевая архитектура и последовательности: [CLIENT_SERVER_MODEL.md](../docs/architecture/CLIENT_SERVER_MODEL.md).

| Каталог | Состояние |
|---|---|
| `sv-client/` | Qt Quick GUI с крупными touch-targets, управлением ракурсом/масштабом и панелью server state, pipeline timings и камер. Qt 5/6 host; Aurora integration и device acceptance ещё нужны |
| `svctl/` | Рабочий CLI без Qt: текущие команды через библиотеку и JSON ACK; будущие config/subscription API появятся вместе с сервером |
| `sv-simulator/` | Qt 6 laptop GUI через `sv-client-lib`: Unix/TCP, IPC discovery, диагностика и визуальный driving preview на Qt Quick 3D с клавиатурным управлением. Driving preview пока не формирует live-кадры, calibration observations или pose-error experiments; редактор мира/камер остаётся открытым |

Небольшой installed-package consumer перенесён в `tests/fixtures/client-consumer/`: это тестовый probe, а не четвёртый продуктовый клиент. Сборка выполняется корневым CMake; старое имя бинарника `sv-client` сохраняется.
