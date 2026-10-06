# Примеры клиентов sv-server

Все пользовательские C++ клиенты используют `sv-client-lib` (`sv::client`). Они не редактируют серверную конфигурацию напрямую. Целевая архитектура и последовательности: [CLIENT_SERVER_MODEL.md](../docs/architecture/CLIENT_SERVER_MODEL.md).

| Каталог | Состояние |
|---|---|
| `sv-client/` | Существующий Qt Quick GUI перенесён из `src/client`; увеличены кнопки, оставлены ракурсы/масштаб, добавлена панель state. Qt 5/6 host; Аврора требует адаптации и приёмки |
| `svctl/` | Рабочий CLI без Qt: текущие команды через библиотеку и JSON ACK; будущие config/subscription API появятся вместе с сервером |
| `sv-simulator/` | Зафиксирован контракт laptop GUI; приложение Qt 6 и live world ещё предстоит реализовать |

Небольшой installed-package consumer перенесён в `tests/fixtures/client-consumer/`: это тестовый probe, а не четвёртый продуктовый клиент. Сборка выполняется корневым CMake; старое имя бинарника `sv-client` сохраняется.
