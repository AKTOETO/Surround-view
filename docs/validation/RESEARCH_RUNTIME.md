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

Четыре native fusion-кандидата и temporary surface apply добавлены и проверены: [[validation/NATIVE_FUSION]]. Unix/TCP сценарий теперь включает 7 вариантов, 21 sample (7 warmup/14 measurement), explicit cube и baseline surface. Rollback при expiry/disconnect покрывает геометрию. Предыдущий 6-entry прогон выше — исторический; на этом этапе полная регрессия составляла 43/43 entries (исторический прогон).

## GUI runtime adapter без Python

`RuntimeSettings` в `examples/sv-simulator` читает state/fusion_catalog/surface_catalog и вызывает типизированные `configure_fusion`/`configure_surface` из клиентской библиотеки. Ввод JSON является формой редактирования снимка, не новым wire protocol. Ревизия захватывается при загрузке каждого черновика; ответы сервера обновляют actual state, не черновик. На reconnect снимки и черновики сбрасываются. GUI не сохраняет server config и не выполняет fusion локально.

C++ GTest `sv-simulator-runtime-tests`, запускаемый из `client_transports` против настоящего `sv-server` на Unix и TCP loopback, проверяет чтение catalog/state, применение weights, отказ stale revision, отказ неизвестного алгоритма, отказ дробного pyramid_levels без усечения, отказ неизвестного носителя, изменение tessellation surface и восстановление исходных surface/fusion. Python harness дополнительно сравнивает байты server config до/после. QML загружается в существующем offscreen smoke; интерактивная работа редактора мышью и Aurora не квалифицированы. Это проверка управления, не исследование качества сшивки или GUI сценариев.

```sh
cmake --build build
ctest --test-dir build -R 'client_transports|simulator_ipc_discovery' --output-on-failure
```

### Ошибки клиентской очереди и доставка GUI событий

Локальный отказ `Client::command` может приходить как `Event::Error` с command_id, без серверного ACK. `RuntimeSettings::commandFailed` сопоставляет ID и снимает pending; чужая ошибка не завершает текущую операцию. Сообщение «запрос не подтверждён» не утверждает, что сервер отклонил или не применил изменение: при session_lost исход может быть неизвестен. Автоматического повторения команды нет.

Отдельный C++ GTest/CTest `simulator_runtime_errors` воспроизводит настоящий `not_ready_or_queue_full` из библиотеки после неудачного подключения, проверяет выход из pending, сохранение снимка и игнорирование другого command_id. Он запускается без сервера; это не fault injection переполнения очереди работающего сервера.

Оба Qt GUI теперь ограничивают очередь кадров отдельно от управляющих событий: ACK, Error и lifecycle не отбрасываются из-за заполнения frame budget. Отброшенный кадр освобождается через библиотеку; публикация weak client reference между constructor/worker защищена mutex. Это не квалификация длительного overload или отдельного жёсткого лимита GUI control mailbox. Диагностика GUI показывает config_revision, а не state_revision в поле конфигурации.

```sh
ctest --test-dir build -R 'simulator_runtime_errors|client_transports|client_lifecycle' --output-on-failure
```

Проверка 10.10.2026: полная сборка `cmake --build build -j4` успешна; `ctest --test-dir build --output-on-failure` — **45/45**, 58.05 s. Unix/TCP GUI/native adapter regression включён в client_transports; simulator_runtime_errors проверяет локальный отказ. Это Linux/loopback/offscreen результат, не подтверждение длительного overload, реального экрана или Авроры.

### Release budget и сброс GUI сессии

`client_release_budget` — новый C++ GTest на TCP mock: deterministic capacity=1, worker удержан в callback; command submission насыщен, RELEASE должен независимо дойти до data socket. Проверены отказ второй команды и отказ второго RELEASE при занятых соответствующих бюджетах. До исправления тест падал на первом RELEASE с `release submission queue full`; после разделения counters проходит. Callback намеренно блокируется только тестом, это не рекомендуемое поведение клиента. Политика и ограничения: [[engineering/CLIENT_LIBRARY]].

`simulator_session_state` — C++ GTest для обоих Qt adapter: события вводятся на границе consume через test-only friend access. Проверяется очистка прежних job ID/изображения/metadata/runtime snapshot при session loss и disconnect, отсутствие implicit обращения к старой calibration job, заполнение UI явными Unix/TCP endpoint и сохранение текущего соединения/настроек при недопустимом вводе. Отдельный случай проверяет игнорирование позднего image-ready callback автомобильного клиента и различие config/state revisions. QSettings тестов изолированы во временной папке. Это unit boundary injection, а не reconnect с физическим GUI/удалённым устройством; wire/Qt offscreen path отдельно покрывается `client_transports`.

Discovery GTest дополнен слишком длинными ASCII/UTF-8 путями: недопустимый кандидат не должен останавливать переход к следующему endpoint. В автомобильном клиенте и симуляторе кадр и диагностические поля очищаются при смене сессии. Симулятор также очищает implicit calibration job и command tracking; локальный Error завершает ожидание tracked calibration command без утверждения об отказе сервера.

URL image provider теперь содержит session_id/frame_id; C++ GUI boundary test дополнен двумя сессиями с одинаковым frame_id=1. Поздний callback старого URL не создаёт present_submit, callback текущего — создаёт. Это проверка adapter bookkeeping, не измерение физического показа дисплея.

Регрессия 10.10.2026: сборка успешна; полная suite прошла **47/47 CTest entries** (80.98 s). После последнего изменения session-qualified URL повторены затронутые `client_transports` и `simulator_session_state` — **2/2**, 22.63 s. Проверки относятся к Linux, TCP loopback/mock, GUI boundary injection и Qt offscreen; физическая Аврора/two-host/длительная перегрузка остаются открытыми.

### Повреждённые ACK и ошибки прикладной диагностики

В `client_lifecycle` добавлены три TCP mock случая: `accepted` неверного типа, отказ с нестроковым `reason` и корректный отказ. До исправления первая регрессия падала с `malformed ACK silently removed the pending command`: команда удалялась до проверки обязательного поля. Теперь повреждённые ACK не доставляются потребителю, ожидающий ID получает `session_lost`, затем сообщается protocol error. Корректный отказ завершает команду обычным ACK. Вместе с прежними partial handshake/frame deadlines и неверным RGBA payload тест содержит шесть mock режимов. Проверяется контракт неопределённого исхода, а не отсутствие серверного изменения.

В `simulator_session_state` добавлены проверки исключений на GUI-потоке: ACK без прикладного поля очищает частичный runtime snapshot и implicit calibration job; кадр без диагностики очищает изображение автомобильного клиента, последующие queued события и команды не заменяют сообщение об ошибке; пустой Message event не выходит исключением в Qt event loop. Симулятор допускает явное повторное подключение, автомобильный пример требует перезапуска. Событийная инъекция выполняется в потоке владельца; blocking `Client::stop()` не вызывается из worker callback.

Отдельный случай проверяет целые JSON значения углов/расстояния. Проверка session-qualified image URL дополнена целыми нулевыми timings обоих адаптеров: GPU draw отображается как `0.00 мс`, а не отсутствие timer. Поддержка чисел не означает принятия строк вместо чисел. Транспортная проверка ACK и проверка прикладных полей GUI имеют разные границы; полного schema validation всех ответов здесь нет.

Проверка 10.10.2026: `cmake --build build -j4` успешна; `ctest --test-dir build --output-on-failure -j4` — **47/47 CTest entries**, 23.69 s. Новые случаи входят в существующие entries, поэтому число entries не увеличилось. Это Linux/mock/loopback, Qt adapter unit и offscreen проверки; физическая Аврора, удалённые две машины и длительный overload этим результатом не подтверждаются.

Catalog v3 и C++ scenario runner дополнены `pyramid_boundary=zero|normalized`. Сценарий `configs/research/pyramid-boundary-screen.json` сравнивает два pyramid fusion × две политики. Native 36-sample READY replay screen восстановил исходные fusion/surface/pause; отрицательный NO_INPUT прогон также сохранил restored=true. GUI typed apply проверен по Unix/TCP. Raw, формулы и ограничения: [[PYRAMID_BOUNDARY]]. Многокадровый runner и независимая quality серия остаются открытыми.
