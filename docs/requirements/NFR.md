# Нефункциональные требования

## Производительность

| ID | Приоритет | Требование | Критерий |
|---|---|---|---|
| NFR-P-001 | MUST | В MVP сервер должен поддерживать четыре входа и стабильный real-time режим. | Не менее 30 FPS на зафиксированной конфигурации hardware, input resolution и mesh density. |
| NFR-P-002 | MUST | End-to-end latency должна измеряться от simulator capture до UI receipt/display path. | Все timestamps доступны в trace. |
| NFR-P-003 | MUST | Должны измеряться CPU time, GPU time, FPS, drops и queue depth. | Есть CSV/JSON telemetry и сценарий измерения. |
| NFR-P-004 | MUST | Изменение virtual camera не должно повторно обрабатывать исходные видеопотоки на CPU. | Профиль и архитектура подтверждают reuse GPU scene. |
| NFR-P-005 | SHOULD | Система должна иметь ограниченный jitter frame delivery. | Значение фиксируется экспериментом для целевой платформы. |

## Конфигурируемость

| ID | Приоритет | Требование | Критерий |
|---|---|---|---|
| NFR-C-001 | MUST | Размеры автомобиля, камеры, calibration, resolution и bowl parameters должны задаваться конфигурацией. | Изменение JSON/YAML не требует recompilation. |
| NFR-C-002 | MUST | Конфигурация должна иметь schema/version и валидацию. | Ошибки сообщаются до запуска pipeline. |
| NFR-C-003 | SHOULD | Новый input adapter не должен менять геометрию и shaders. | Adapter реализует общий интерфейс. |

## Надежность и диагностика

| ID | Приоритет | Требование | Критерий |
|---|---|---|---|
| NFR-R-001 | MUST | Сбой одного источника не должен зависать весь сервер. | Fault injection завершает тест с health status. |
| NFR-R-002 | MUST | Все компоненты должны иметь структурированные логи. | В логах есть component, severity, timestamp и correlation ID. |
| NFR-R-003 | MUST | Ошибка конфигурации, транспорта или GPU должна быть диагностируема. | Сообщение содержит источник и код ошибки. |
| NFR-R-004 | SHOULD | Сервер должен поддерживать graceful shutdown и освобождение GPU/IPC ресурсов. | Повторный запуск не требует ручного удаления ресурсов. |

## Сопровождаемость и переносимость

| ID | Приоритет | Требование | Критерий |
|---|---|---|---|
| NFR-M-001 | MUST | Компоненты должны собираться через CMake на Linux. | Чистая сборка выполняется documented-командой. |
| NFR-M-002 | MUST | Server, UI и simulator должны иметь независимые entry points. | Каждый процесс можно запустить отдельно. |
| NFR-M-003 | MUST | Протокол должен иметь versioning и backward/forward compatibility policy. | Несовместимость определяется до обработки payload. |
| NFR-M-004 | SHOULD | Математика проекции должна иметь CPU reference implementation. | Есть unit/golden tests. |
| NFR-M-005 | MUST | Тестовые сценарии должны быть воспроизводимыми. | Фиксированные config, input data и seed дают сопоставимый результат. |

## Ограничение смысла real-time

До проведения экспериментов нельзя заявлять универсальное «реальное время». В отчете всегда указывать GPU, driver, OS, input resolution, mesh density, transport и measured FPS/latency.
