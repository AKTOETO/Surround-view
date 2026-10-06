# sv-simulator: целевой инженерный GUI на ноутбуке

**Статус: приложение Qt 6 ещё не реализовано.** Каталог фиксирует третий продуктовый клиент; наличие README не означает готовность simulator binary. Существующие offline generators/Blender export продолжают работать до переноса, чтобы сохранить воспроизводимость мира и измерений.

Архитектура, шесть PlantUML workflow и приёмочные сценарии: [CLIENT_SERVER_MODEL.md](../../docs/architecture/CLIENT_SERVER_MODEL.md).

Планируемые разделы GUI:

- Подключения Unix/TCP через `sv-client-lib`, capabilities, state/health.
- Полная серверная настройка через versioned transactions; никаких прямых записей server config.
- Источники replay/реальные/виртуальные, dataset/manifest и recorder.
- Калибровка, carrier/fusion, final/intermediate subscriptions, pipeline trace.
- Карта/мир, автомобиль, rig четырёх камер, движение, pause/reset, simulation time и pose truth.
- Аналитические fixtures, photographic demo, Blender world, сравнение эталона и experiments/reports.

GUI использует application services; существующие offline jobs сначала допускают subprocess adapters с progress/cancellation, затем их логика переносится в библиотеки. Мир и camera producer отделены от клиентского control/output API. Внешний ноутбук направляет virtual images на camera endpoints целевого сервера; получение продуктов идёт по client session.
