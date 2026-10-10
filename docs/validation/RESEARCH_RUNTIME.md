# Проверка управляемого исследовательского runtime

Дата: 10.10.2026. Реализация: `examples/common/research`, `svctl research`; контракт: [[engineering/PROTOCOL_IMPLEMENTED]], команды: [[engineering/USAGE#Сценарное сравнение fusion через sv-client-lib]]. Это инженерная проверка первого runner, не подтверждающая выборка сшивки.

## Проверяемые свойства

| Проверка | Исполнение | Критерий |
|---|---|---|
| Schema и бюджет | GTest `ResearchScenario` | Reject unknown/version/modes/fields, invalid parameters, fractional counters и превышение бюджета до открытия соединения |
| Cancel до соединения | GTest `ResearchScenario` | Failed report без mutation/restore; scenario hash присутствует |
| CLI → настоящая sv-client-lib → сервер | `client_transports`, Unix/TCP/combined profiles | 3 variants × (1 warmup + 2 measurement blocks) = 9 кадров, каждый block полный |
| Парность | Та же интеграция | Все samples используют одинаковые inputs/frame-set ID, upload_count не меняется |
| Повторяемость | Та же интеграция | SHA-256 RGBA каждого варианта одинаков между блоками |
| Отчёт | Та же интеграция | Server fingerprint/scenario SHA, raw samples, p50/p95, measurement count=2 без warmup |
| Восстановление | Та же интеграция | Fusion и RGBA возвращаются к исходным; config file побайтно неизменен |
| Отмена и исключение progress | GTest `ResearchScenarioIntegration`, запускаемый из `client_transports` на живом endpoint | После первого sample серия failed, restore успешен, SHA исходного и восстановленного RGB совпадает |

Native integration GTest требует `SV_RESEARCH_TEST_ENDPOINT` и отдельно пропускается без живого сервера; Python harness предоставляет endpoint и запускает его автоматически. Таким образом live-проверки не заменяются одним skipped standalone test.

```bash
cmake --build build -j 4
ctest --test-dir build -R '^(research_scenarios|svctl_options|client_transports|replay_ipc)$' --output-on-failure
```

Финальный регрессионный запуск на Linux: 6/6 CTest entries прошли — `client_lifecycle`, `replay_ipc`, `client_transports`, `config_store_persistence`, `research_scenarios`, `svctl_options`. `client_transports` дополнительно запустил native cancellation/failure GTest с живыми Unix/TCP endpoints; standalone skip не использован как свидетельство этой проверки.

## Границы свидетельств

Нет собственного truth или данных для рейтинга алгоритмов. Это один текущий paused frame set, не clip/independent scenes. Frame hashes проверяют повторяемость output, а не целостность исходного dataset. Тайминги повторно используемых GPU resources не описывают поток с capture/decode/upload. Samples хранят settings, actual order, server source fingerprint и GPU device; для финального опыта дополнительно нужны input content hashes, dataset split, frozen metrics/criteria, аппаратный паспорт и CPU/GPU/thermal profiles.

Ограниченный серверный lease/watchdog fusion/surface/pause реализован и проверен ниже. General transaction/history restore, lost-ACK recovery, durable progress, полная GPU/offline raster parity и simulator GUI остаются открытыми: [[architecture/RESEARCH_RUNTIME]], [[../TODO]].


## Проверка server lease/watchdog

GTest `experiment_lease` проверяет ownership/token, пределы TTL, точную границу истечения, renew, запрет conflicting persistence/view changes, старые IDs после завершения и failed state. Python `experiment_watchdog` на настоящем сервере проверяет expiry/renew, конфликты, восстановление исходного fusion/surface/pause без записи config file, закрытие control и повторное подключение. Отдельно проверяются исходные running и paused источники; новый клиент начинает следующий lease после idle. Закрытие сокетов моделирует обнаруживаемый EOF, но не half-open сеть.

`client_transports` выполняет runner с lease через Unix/TCP, cancellation/failure GTest также проходит через новый контракт. ACK release проверяется как начало recovery; runner ждёт idle и сохраняет final_state. Конечные state/fusion должны совпасть с исходными. Предельный 5 s timeout source recovery и зависший GPU не проверены физическим fault injection; кооперативный watchdog не считается hard realtime.

```bash
ctest --test-dir build -R '^(experiment_lease|experiment_watchdog|research_scenarios|client_transports|replay_ipc)$' --output-on-failure
```

Регрессия после интеграции lease: 8/8 CTest entries прошли (`client_lifecycle`, `experiment_watchdog`, `replay_ipc`, `client_transports`, `config_store_persistence`, `experiment_lease`, `research_scenarios`, `svctl_options`). После добавления invalid owner/baseline/TTL controls отдельно повторены `experiment_lease` и `experiment_watchdog`.

## Расширение серверных алгоритмов и геометрии

Четыре native fusion-кандидата и temporary surface apply добавлены и проверены: [[validation/NATIVE_FUSION]]. Unix/TCP сценарий теперь включает 7 вариантов, 21 sample (7 warmup/14 measurement), explicit cube и baseline surface. Rollback при expiry/disconnect покрывает геометрию. Предыдущий 6-entry прогон выше — исторический; текущая полная регрессия — 43/43 entries.

## GUI runtime adapter без Python

`RuntimeSettings` в `examples/sv-simulator` читает state/fusion_catalog/surface_catalog и вызывает типизированные `configure_fusion`/`configure_surface` из клиентской библиотеки. Ввод JSON является формой редактирования снимка, не новым wire protocol. Ревизия захватывается при загрузке каждого черновика; ответы сервера обновляют actual state, не черновик. На reconnect снимки и черновики сбрасываются. GUI не сохраняет server config и не выполняет fusion локально.

C++ GTest `sv-simulator-runtime-tests`, запускаемый из `client_transports` против настоящего `sv-server` на Unix и TCP loopback, проверяет чтение catalog/state, применение weights, отказ stale revision, отказ неизвестного алгоритма, отказ дробного pyramid_levels без усечения, отказ неизвестного носителя, изменение tessellation surface и восстановление исходных surface/fusion. Python harness дополнительно сравнивает байты server config до/после. QML загружается в существующем offscreen smoke; интерактивная работа редактора мышью и Aurora не квалифицированы. Это проверка управления, не исследование качества сшивки или GUI сценариев.

```sh
cmake --build build
ctest --test-dir build -R 'client_transports|simulator_ipc_discovery' --output-on-failure
```
