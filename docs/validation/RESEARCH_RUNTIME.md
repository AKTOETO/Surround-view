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

## Последовательный native runner, 11.10.2026

Прежнее ограничение одним frame set снято: `frames=1…256`, native measurement capture consumer, per-frame baselines/summaries и защищённый step. Cursor/history по-прежнему не восстанавливаются. Новый hello capability `experiment_step_v1` проверяется до мутаций; старый default frames=1 сохранён.

Дополненные GTest/schema/lease и live Unix/TCP проверки покрывают:

- бюджет с учётом frames, fractional counters и отсутствие capture consumer до соединения;
- три кадра × два boundary profiles × два measurement blocks, 12 сохранённых RGBA с проверкой размеров/hashes и разных frame sets;
- одинаковые inputs внутри кадра, восстановление последнего baseline RGBA и неизменность server config;
- отмену после первого sample второго кадра и исключение capture consumer с успешным restore;
- повторный CLI запуск с существующей capture папкой без перезаписи прежнего report;
- stale paused NO_INPUT → один owner step → READY, затем второй кадр (`replay_ipc`); wrong-owner/expired step отклоняется native lease guard.

Реальный исследовательский запуск: 76 условий, 684 samples, 532 captured measurement frames, все шесть запусков восстановили настройки. Independent direct-view audit и отрицательные результаты описаны в [[validation/SERVER_BOUNDARY]]. Это short paused sequence, не continuous-stream performance/temporal-history квалификация. Произвольный seek, durable checkpoint, reset history и GUI сценариев остаются открытыми.

Итоговая Linux регрессия 11.10.2026: полная сборка успешна; CTest **50/50**, 29.12 s. Первый прогон нового NO_INPUT теста выявил пропущенное поле schema_version в тестовом scenario; после исправления fixture отдельно прошёл replay_ipc и затем вся suite. Это не обнаруженный дефект server schema validation.

## Multilabel candidate, catalog v4

Typed library/scenario/GUI adapter поддерживают optional seam_solver. Unix/TCP live native GUI-adapter test применяет graph_cut_multi_band + normalized + alpha_expansion, проверяет snapshot и прежний stale-revision/restore flow. CLI sequential capture regression переключает binary/alpha, проверяет одинаковые inputs, solver setting и монотонную energy/sweep budget actual frame diagnostics; legacy frames не сохраняют старые alpha stats. Schema/core отвергают unknown solver. Legacy default mathematical parity suites сохранены; новые alpha controls используют exhaustive graph oracle ([[validation/MULTILABEL_SEAM]]).

После total-capacity guard финальная полная сборка успешна, CTest **51/51**, 22.54 s. Нативный GTest содержит шесть случаев, в том числе 512 exhaustive moves и 128 complete label enumerations; это не 512 независимых визуальных сцен. Синтетический over-limit graph отклоняется до max-flow, runtime перегрузка и RSS/thermal на target ещё не квалифицированы.


## 17.10.2026 — матрицы и versioned trial plan

Общий C++ runner получил bounded Cartesian expansion и фиксированную перестановку без
std::shuffle/uniform_int_distribution; plan/hash и trial_index позволяют проверить полный
план и выполненный префикс. Проверки: golden schedule, complete blocks/frames, seed repeat,
trial budget, axis order/invalid matrix, exclusive report reservation/dangling symlink.
Live Unix/TCP использует matrix для трёхкадровой capture серии; проверены plan/sample identities,
restore при cancel/callback/capture failure и сохранность прежнего metadata-only report.

Полная Linux сборка; research_scenarios/svctl_options2/2 за0,01 с и client_transports1/1 за23,05 с.
14 GTest в scenario target: live case пропускается standalone, но выполняется с endpoints
интеграционным harness. Все25 PlantUML blocks runtime/главы3 прошли local1.2026.6 -checkonly.
Это инженерная проверка runner, не новая E-STITCH quality series. Новый order algorithm может
изменить порядок по сравнению с историческим std::shuffle; прежние результаты не переписаны.
Reset cursor/cache/history, scene orchestration, GUI и durable resume не реализованы.


## 17.10.2026: управляемый replay reset

Реализованы server capability/command replay_reset_v1/replay_reset, typed
Client::reset_replay и optional scenario.reset_replay. Source worker публикует явную границу
после cache clear/cursor0/new session; render thread очищает историю Synchronizer до новых frames.
Sequence/frame_set_id и lifetime counters сохраняют монотонность; source session отличается.
Это decode-cache reset; GPU/OS cache не очищается, cursor после lease не возвращается.

| Проверка | Наблюдаемый результат |
|---|---|
| sources native | Два reset подряд: boundary → first Frames → Control; +4 loader calls при одинаковых путях; paused после ACK; новый session/растущий sequence |
| core native | После reset_history нет retained inputs/ordering guards; READY после полного нового набора; диагностические counters не обнулены |
| client_transports Unix/TCP profiles | Два трёхкадровых matrix прогона дают одинаковые sample RGB hashes и scenario timestamps, разные sessions и растущие frame_set IDs |
| Неполная строка0 | После reset camera0 used=false, DEGRADED; +3 декодирования; step читает строку1; старая камера не сохраняется |
| Runner на неполной строке0 | Failed report, samples=[], ready_preparation=reset_first_row, restored=true; hidden step отсутствует |
| Lease controls | Без token/с чужим token reset отклонён; leased runner cancellation/progress/capture errors восстанавливают fusion/surface/pause |
| camera_sources | Socket source отклоняет replay_reset; обычный producer/camera workflow сохранён |

Полная Linux сборка `cmake --build build -j4` завершилась успешно. В первом прогоне прошли
sources/core/camera_sources/source_work_timing/experiment_lease и watchdog/scenario tests, но
client_transports обнаружил ошибку новой fixture: отсутствующая камера была обозначена пустой
строкой вместо manifest null. После исправления fixture и расширения lease/cancellation coverage:

```sh
ctest --test-dir build -R '^(client_transports|experiment_watchdog|research_scenarios)$' --output-on-failure
```

Повторный прогон3/3 за29,05 с (client_transports26,02 с, watchdog3,02 с).
Остальные5 затронутых entries прошли ранее: sources0,11 с, core0,00 с, camera_sources16,15 с,
source_work_timing3,01 с, experiment_lease0,00 с. Полный66-entry suite не запускался.
Проверен синтаксис27 PlantUML blocks architecture runtime/главы3 через локальный PlantUML jar;
визуальная приёмка PDF/Аврора/физические камеры в этот этап не входят.

Это проверка воспроизводимости инфраструктуры, не подтверждающее исследование качества сшивки.
Следующие задачи: independent dataset/scene/mount/noise orchestration и quality-oracle outputs,
затем GUI сценариев и durable resume; cursor snapshot/restore и GPU cache policy остаются отдельными.


## 17.10.2026: C++ masked RGB reference evaluator

QualityReference/quality_metrics добавлены в общий sv-research service; CLI загружает manifest
до report reservation/подключения. Truth/mask серверу не передаются. Server capability
frame_view_v1 добавляет в FRAME полный snapshot azimuth/elevation/distance/FOV/clip/target.
Контракт и рабочий пример: [[engineering/RESEARCH_QUALITY]].

| Проверка | Результат |
|---|---|
| Известные RGB errors3/4/0, один выбранный пиксель | MAE=7/3, RMSE=sqrt(25/3), max=4, PSNR=38.92261606915535 dB |
| Perfect match | exact_match=true, psnr_db=null; alpha/исключённые пиксели не влияют |
| Invalid/empty masks и buffers | Отказ, включая nonbinary и неодинаковые RGB каналы mask |
| Manifest/guards | Strict fields/schema, four ordered guards, canonical timestamps; view/format/input mismatch отклоняется |
| Projection/memory bounds | Неверный FOV/clip/target и pixel budget >128 MiB отклоняются до загрузки изображений |
| Сценарий без переданного evaluator | Failed report до подключения/restore_required=false |
| Summary repeated frames | Пять repeats кадра0 + кадр1 дают2 frames, warmup исключён; изменённые повторные metrics обнаруживаются |
| Настоящий сервер, Unix/TCP | Три frames regression references, alternating binary ROI: measured MAE/RMSE0, warmup без quality, summary counts каждый frame один раз |
| Изменённый reference view | Guard mismatch, failed report с restored=true, измеренных samples нет |
| Malformed reference kind через CLI | Отказ до report reservation; output file не создаётся |

Полная Linux сборка `cmake --build build -j4` успешна. Финальный прогон:

```sh
ctest --test-dir build -R '^(client_transports|research_scenarios|svctl_options|replay_ipc|output_products)$' --output-on-failure
```

5/5 за45,01 с: replay_ipc6,30 с, client_transports28,84 с, output_products9,86 с,
research_scenarios/svctl_options0,00 с. Scenario executable содержит21 GTest: standalone20 passed,
1 live case skipped без endpoint; этот case выполняется через client_transports на Unix/TCP.
Проверен синтаксис29 PlantUML blocks runtime/quality/главы3 локальным jar. Полный66-entry suite,
SDK/Аврора и physical cameras не проверялись.

Regression reference намеренно получен из предыдущего native capture и явно помечен regression.
Нулевые ошибки здесь подтверждают корректность интеграции и повторяемость, **не качество сшивки**.
Kind=independent_ground_truth остаётся декларацией автора; dataset_id и IDs/timestamps не
обеспечивают content identity camera images. Independent holdout, source/config hashes, заранее
заданные критерии, другие метрики и подтверждающая серия ещё открыты. Success — статус workflow,
не прохождение порога scientific quality. Новое ранжирование fusion методов не выполнялось.


## 17.10.2026: content/model guards replay

Server/replay worker добавляют packed_rgb8_sha256_v1 и camera_model_sha256_v1; manifest schema2
требует оба guards каждого input. Schema1 сохранена с явной legacy metadata-only маркировкой.
Known-answer RGB/camera digests независимо вычислены Python struct/hashlib; C++ unit tests
проверяют изменение bytes/dimensions/intrinsics/extrinsics/k/theta/epsilon, нормализацию -0 и
отказ не-finite. Source test меняет результат loader при прежнем path, проверяет новые pixels/hash
после reset. Unix/TCP integration независимо сверяет все hashes с fixture и меняет один реальный
source pixel при прежнем manifest/IDs/timestamps: получен input guard mismatch, restored=true.

Полная Linux сборка успешна. sources/replay_ipc/client_transports/source_work_timing/research_scenarios
5/5 за38,87 с (0,12 /6,15 /29,59 /3,01 /0,00 с). Scenario GTest24: standalone23 passed/1 skipped,
живой case выполнен Unix/TCP harness. Полный66-entry suite и Аврора не проверялись.
Hash стоимость не объявляется бесплатной: loader span пока исключает hash/cache preparation;
полный dataset/config passport и target overhead измерения остаются открытыми.


## 17.10.2026: thresholds и отдельная quality acceptance

Manifest criteria max_mae_rgb8/max_rmse_rgb8/min_psnr_db проверяются до подключения и включены
в manifest hash. All-measured policy сохраняет все trials, warmup исключён; workflow/restore
и quality outcome разделены. Partial/guard/callback/restore failure не может получить passed.
Финальные unit/CLI checks research_scenarios/svctl_options2/2 за0,01 с; live client_transports1/1
за31,80 с после normalization fix. Ранее replay_ipc прошёл6,49 с. Native GTest suite27 tests
(26 standalone, один live endpoint case выполняется harness). Полная Linux сборка успешна.

Проверены включительные границы, unknown/boolean/string/negative/out-of-range criteria,
perfect-match PSNR, warmup exclusion, failed-frame counts и partial completeness. Live Unix/TCP:
нулевые regression errors проходят; изменённый truth pixel нарушает thresholds, сохраняются все9
samples, success/restored=true, status=failed, CLI5, failed_frame_variants=1. Подмена source pixel
с прежними IDs/timestamps даёт guard error и status=incomplete. Первый unit прогон выявил ошибку
проверки теста: сравнивались strict JSON integer/double kinds вместо числовых значений; тест
исправлен. Заодно исправлен product view guard, который мог отвергать математически одинаковые
значения8/8.0; native test теперь проверяет такую normalization. Допуск/округление не вводились.

Полный66-entry suite/Аврора не запускались. Эти критерии — механизм инструмента, не обоснованные
нормы качества; независимая подтверждающая выборка и научная preregistration всё ещё нужны.


## 17.10.2026: direct Blender fixture и полная регрессия

Добавлен CTest research_quality_fixture: существующий frozen paired_street_v1, content-verified
schema2 references, три direct virtual RGB/ROI,4 variants×3 frames×2 repeats=24 samples.
Python только проверяет capture/file provenance, подготавливает reference fixture и сравнивает
C++ metrics с NumPy:96 comparisons, max difference0. ROI counts44733/44804/44788; sums134325.
NVIDIA RTX5070Ti run сохранён в artifacts/q-native-v1 (report/RGBA/trace/oracle-check); Mesa case
прошёл в полном CTest. Figure визуально проверена; генератор рядом с главой3, рисунок3.48.
Это reused low-detail scene, не holdout, и quality thresholds не заданы.

Полный `ctest --test-dir build --output-on-failure -j2` завершился **67/67** за55,75 с, включая
camera MOCK/recovery, protocol/listeners, GUI/QML, telemetry, packaging checks, numerical oracles
и native rendering/evaluation. Наличие Linux packaging tests не означает реальную RPM сборку
в Aurora SDK или запуск на устройстве. Проверен syntax31 PlantUML blocks после §3.48–3.49.
Blender MCP теперь подключён; текущая сцена на момент проверки — стартовый Cube/Camera/Light.


## 17.10.2026: новые environment families, matched carrier screen

Live Blender MCP построил street-a/b и cargo yard-a/b с независимыми scene/mount streams;
первоначальная Scene/Cube сохранена. Pure strict family-plan preflight и cancel-before-first-case
проверены; cancel дал0 completed. Actual family/origin/layout/offsets и generator hashes сохранены.
В4-case workflow smoke64 measured samples/256 metric comparisons совпали. Затем20-variant
native screen дал640 trials/480 measured samples/1920 metric comparisons; max difference
7,105427357601002e−15. Active triangles3196…3208, buffers58392…60168 bytes; все RGB repeats
идентичны.40/40 mean contrasts normalized−zero и80/80 frame contrasts отрицательны.
Это validation pilot с true supplied calibration, без нормативных thresholds; geometry artifacts
остаются. Полный протокол/таблицы/изображения: [[validation/FAMILY_CARRIER_SCREEN]], §4.93.

Исправлены deep-output IPC failure и неоднозначные basename dataset IDs в test adapter;
IPC теперь отдельный короткий temp path, ID derived из capture hash. Добавлен bounded child
cleanup; custom native scenario/visibility-policy поддерживаются fixture harness. После итоговых
изменений `cmake --build build -j4` успешен и повторный полный CTest **67/67 за54,78 с**.
На момент full regression Python Blender fixture содержал18 cases; после добавления проверок
render cancellation/missing/empty output —19. Итоговый targeted CTest blender_fixture,
stitch_robustness,research_quality_fixture прошёл3/3. Native GTest research содержит27 cases
(live endpoint проверяется harness). Реальный Aurora SDK/device и physical cameras
по-прежнему не квалифицированы.

## 17.10.2026: compiled batch service одной записи

Реализован `run_batch` над общим ExperimentRunner и PImpl FileExperiment; одиночный CLI
также использует FileExperiment. Полная подготовка всех scenario/reference и output reservations
предшествует первому подключению. Raw plan/scenario hashes дополняют semantic trial hashes.
Контракт и пример: [[engineering/RESEARCH_BATCH]], configs/research/batch-repeatability.json.

Добавлено 7 GTest cases: pre-cancel без сети; preflight позднего missing scenario/reference;
strict ID/version/member bounds; reset/aggregate trial budget; immutable scenario/single-attempt;
report aliases/dangling symlink; failed capture reservation. Общий research binary теперь
содержит 34 cases: standalone 33 passed/1 live skipped; live case исполняется transport harness.
CLI options — 8/8 cases, включая batch arity. Targeted CTest unit/CLI — 2/2 за 0,01 с.

`client_transports` проверил Unix-only, TCP-only и combined profiles: две последовательные
серии reset/capture дают одинаковые RGB hashes; каждый сценарий возвращает lease в idle.
Quality rejection первого участника не мешает второму, aggregate exit 5 сохраняет success/restore.
Подмена source pixels даёт exit 4/incomplete: первый отчёт записан и restore подтверждён,
второй участник остаётся not_started с пустым reserved report. План/scenario raw hashes
независимо сверены hashlib. Targeted client_transports + research_quality_fixture — 2/2 за 37,46 с.

После финальных изменений полная Linux сборка успешна; **CTest 67/67 за 56,36 с**.
Проверены syntax четырёх PlantUML blocks RESEARCH_RUNTIME, включая новую batch sequence,
clang-format и diff whitespace. Blender MCP отвечает, версия 5.2.2 LTS; активная сцена —
SV Research Cargo Yard.001. Новый исследовательский захват не выполнялся: задача этого среза —
нативная оркестрация. Multi-recording switching, GUI editor, whole-batch isolation, durable resume,
старый cursor и Aurora/physical acceptance не считаются закрытыми.

## 18.10.2026: GUI одиночного исследования

Добавлены ResearchPanel, ResearchController/PImpl и helpers typed draft/result rows. Исполнение
через общий C++ FileExperiment идёт вне UI thread. Atomic progress poll 100 мс не создаёт event
на каждый trial. Входные формы/endpoint фиксируются, вывод — scenario.json, report.json и measured
RGBA. Без внешнего reference GUI не объявляет scientific acceptance и не подставляет нули в MAE.
Контракт и воспроизведение: [[engineering/RESEARCH_BATCH#Исследование из sv-simulator]], §3.53.

Пять unit GTest cases проверяют expansion значимых axes, strict types/methods/budget, отказ без
соединения, исключение worker при существующем output без перезаписи, отсутствие/нулевые metrics.
Три live cases запускаются для Unix/TCP/combined profiles: 16-trial workflow сохраняет report и
restore, UI heartbeat продолжает работать; изменение viewer availability не меняет frozen endpoint;
cancel после первого trial даёт partial report и подтверждённый restore; destructor выполняет
cancel/join, после удаления controller обработаны оставшиеся UI events. Config-файл сервера
побайтно сохраняется. Итого девять live case executions внутри client_transports.

Два QML cases: изменение frames/repeats, сбор typed draft и переключение трёх вкладок без warnings;
ResearchCloseGuard отклоняет close при busy, запрашивает cancel, закрывает окно после terminal
callback и очищает closeWhenDone. Window case использует отказ подготовки без сети, настоящий
restore отдельно проверен live cases. Screenshot research_editor.png получен из настоящей формы
с UI fixture планом 48 trials; это не measured result. Контраст палитры проверен визуально.
`sv-simulator --smoke` загружает main.qml с настоящим viewer-подключением в transport harness.

Linux/Qt 6.11.2: полная сборка успешна. После финального window guard полный CTest **68/68 за
58,13 с**; client_transports 39,28 с, simulator_configuration_qml 6,77 с. Отдельный запуск новых
QML cases — 2/2. Проверен syntax 33 PlantUML blocks в runtime/chapter3, clang-format и whitespace.
Qt5/Aurora/device acceptance этим запуском не подтверждается. GUI batch editor, general matrices,
capability-driven choices, внешний reference/import, сравнение нескольких reports и durable resume
остаются открытыми. Новые Blender captures и научный reranking в этом продуктовом срезе не нужны
и не выполнялись.

## 18.10.2026: внешний reference и single-scenario import в GUI

Форма выбирает local reference через file dialog, либо запускает существующий scenario.
Оба пути используют тот же C++ FileExperiment, включая preflight до создания output/сети.
Raw byte input provenance включает memory/file kind, base и original path для файла.
Отчёт сохраняет загруженный reference passport и при ранней отмене без evaluated frames.
Original scenario не изменяется и не копируется в bundle. Контракт: [[engineering/RESEARCH_BATCH]].

Общий evaluator добавил pooled PSNR/exact flag. Контроль RMSE 1/3 при ROI weights 1/3 даёт
PSNR 39,67982320853653 dB; пять repeats первого frame не меняют weights. Отдельно проверены
exact match/null PSNR и отсутствие PSNR у варианта без measured frames. Memory input тест
проверяет raw-byte identity, base, bounds и отсутствие создаваемого source file.
Research GTest binary: 36 cases, standalone 35 passed/1 live skipped, live — в transport harness.

GUI unit binary теперь имеет 7 standalone cases и отдельные live profiles. Проверены local URL
validation (включая query/fragment), filenames с пробелами/#/%, missing reference до output,
malformed imported input, отображение малых ненулевых ошибок и недоступных метрик.
Отказ нового запроса очищает прежние badge/rows/report path, не удаляя прежние файлы.

Transport harness запускает форму и импорт при четырёх состояниях: passed, failed,
not_requested с metrics и incomplete после source-pixel tampering. Это 4×2×3=24 reference
workflow executions для Unix/TCP/combined profiles, не 24 независимых scenes. Каждый возвращённый
отчёт проверяется на restore и scientific/workflow status. Criteria берутся из заранее загруженного
manifest; отсутствие criteria не переименовывается в passed. Для incomplete без criteria
acceptance мог бы оставаться not_requested: workflow оценивается отдельно.

Три дополнительных QML live executions действительно запускают импорт кнопкой и рендерят
полученные rows в окне. Screenshot research_reference_results.png — реальный regression
transport fixture с тремя frames/шестью measured samples, не независимая quality truth.
Timing numbers на изображении не используются для qualification. Source fixtures воспроизводятся
из tests/test_client_transport.py; временные datasets/reports очищаются по завершении теста.
QML standalone проверяет open/reject dialog, выбор URL без auto-start, tabs и close guard;
reference workflow case отдельно запускается harness и skip без endpoint/fixture.

После финальной сборки Linux/Qt 6.11.2 полный **CTest 68/68 за 73,91 с**; client_transports
55,57 с, simulator_configuration_qml 6,81 с. До добавления последнего not_requested control
targeted client_transports/research_quality_fixture также прошли 2/2. Bounds/schema/API не
подменяют научное обоснование критериев, независимые сцены или Aurora/device acceptance.
Qt5 wrapper подготовлен по прежней структуре dialog, но Qt5 runtime в этой проверке отсутствует.
Остаются GUI batch/general matrices, capability-driven choices, criterion editor/bundles,
сравнение разных reports, durable resume и прежние исследовательские/целевые gates.


## 18.10.2026: GUI запуска сохранённого batch-плана

Реализованы третий режим плана и прямой вызов C++ run_batch из worker. Никаких новых
wire-команд либо Python dispatcher. До сети проверяются все inputs и резервируются outputs;
каждый участник сбрасывает replay и восстанавливает fusion/surface/pause по прежнему контракту.
Начальное progress событие member/0/total не позволяет наследовать завершённый счётчик
предыдущего участника. GUI передаёт только компактные member rows, не весь report/RGBA.

Проверки `simulator_research_unit` дополнены late-member failure (output не создаётся),
отклонением remote URL и отсутствующими workflow/restore evidence в not_started/error.
`client_transports` запускает для Unix-only, TCP-only и combined:

- native GUI controller: завершение двух сценариев и отмену первого длинного сценария;
  endpoint после старта снимается из availability, работа остаётся на исходном сервере;
- batch с двумя reference сценариями при passed/failed/not_requested/incomplete;
  failed не останавливает второго участника, incomplete оставляет его not_started;
- настоящий QML workflow: выбор режима batch, кнопка запуска, worker, две result cards
  и screenshot без QML warnings. Standalone QML также проверяет выбор URL без auto-start.

Сводки проверяют terminal exit/quality, число reported/completed и подтверждение restore.
Старые single-scenario/CLI проверки сохранены. Targeted research_scenarios,
simulator_research_unit и simulator_configuration_qml прошли 3/3 за6,79с;
client_transports прошёл за76,12с. QML изображение главы3 — фактический результат Linux
Qt6.11.2 на regression fixture, не независимая truth и не аппаратный benchmark.

Воспроизведение: обычная Linux сборка с SV_SIMULATOR=ON, затем
`ctest --test-dir build -R 'client_transports|simulator_research_unit|simulator_configuration_qml|research_scenarios' --output-on-failure`.
Harness сам создаёт replay/reference и live endpoints; после live QML в
`/tmp/sv-reference-result.png.batch.png` остаётся диагностический screenshot.
Standalone reference QML case без endpoint/fixture пропускается; это не считается live evidence.
Qt5/Aurora и целевое оборудование не проверены. Редактор списка участников, multi-recording,
report comparison, durable recovery и независимое подтверждающее исследование остаются открытыми.


Итоговый полный прогон `ctest --test-dir build --output-on-failure` вне ограничений socket
sandbox: **68/68, 152,08с**, последовательно; client_transports76,07с,
simulator_configuration_qml6,79с. Первоначальный ограниченный прогон дал bind/socket
`Operation not permitted`; его отказы относятся к окружению и не используются как evidence
продуктового отказа. Повтор с доступом к локальным сокетам прошёл полностью.
35 PlantUML blocks затронутых архитектурной главы и главы3 прошли `-checkonly`;
clang-format и `git diff --check` не нашли нарушений.

## 18.10.2026: визуальный состав batch и memory overload

`ResearchBatchEditor` добавляет до восьми локальных scenario files, сохраняет ID при
перестановке, удаляет участников и не запускает worker при редактировании. Типы/URL проверяет
C++ builder; документ и endpoint фиксируются до worker. Memory и file overload используют
один execute_batch: прежние strict IDs/limits/reset/reference preflight и общий runner.
После preflight до сети memory вариант сохраняет `inputs/plan.json`, его точные compact JSON
+ newline bytes дают plan_sha256. GUI использует абсолютные paths; относительные ссылки
прямого C++ overload требуют исходной base и не обещают переносимый CLI snapshot.

Добавленные проверки:

- C++ batch: порядок, hash сохранённых bytes, повторное чтение GUI-совместимого snapshot,
  отказ перезаписи, поздний невалидный участник, duplicate IDs и oversize без output;
- C++ Qt builder/controller: типы/URL, literal `#`/`%`, предел8, неправильный поздний scenario
  без output; сохранённые single/reference сценарии также используют общий URL validator;
- QML: пустую серию нельзя запустить, девятый участник не добавляется, настоящие кнопки
  меняют порядок/удаляют строку, повторное добавление получает свободный ID; редактирование
  не запускает исследование;
- live Unix/TCP/combined harness: memory серии с external reference при passed, failed,
  not_requested и incomplete, snapshot в summary; отмена memory серии с restore;
  настоящий QML workflow теперь исполняет single file, saved batch и визуальный состав.

Targeted `research_scenarios`, `simulator_research_unit`, `simulator_configuration_qml` после
финальной сборки прошли3/3 за6,84с. Рисунок3.53 — реальный Linux/Qt6.11.2 интерфейс перед
запуском regression серии, воспроизводимый через client_transports; screenshot сохраняется в
`/tmp/sv-reference-result.png.editor.png`, результат составленной серии — в
`/tmp/sv-reference-result.png.composed.png`. Эти изображения не являются scientific truth
или доказательством целевой производительности. Standalone live QML case по-прежнему требует
endpoint/fixture и пропускается без них. Qt5/Aurora/device acceptance ещё не выполнены.
Редактор не импортирует существующий batch в список и не сохраняет отдельный неисполненный
черновик; dataset bundling/multi-recording/durable recovery и научные gates остаются открытыми.


Итоговый последовательный Linux прогон вне socket sandbox: **CTest68/68 за158,29с**;
client_transports82,22с, simulator_configuration_qml6,82с. Общий runner проверен через CLI,
saved-batch GUI и memory-composition GUI. 36 PlantUML blocks архитектурной главы и главы3
прошли `-checkonly`; Microsoft clang-format и `git diff --check` прошли.

## 18.10.2026: импорт batch в редактор и filesystem semantics

Общий C++ `batch_plan` предоставляет parse_batch_plan/read_batch_plan для импорта и обоих
run_batch overload. Структура проверяется полностью до выдачи replacement rows; сценарии и
references на стадии импорта не читаются. Selected batch directory остаётся base относительных
ссылок, включая symlink самого batch, как в CLI. Входные URL больше не проходят lexical
сокращение `link/..`: regression создаёт два разных scenario files и сравнивает canonical
identity после import → draft roundtrip. Это исправляет реально обнаруженный выбор неверного
файла в прежнем QFileInfo.absoluteFilePath преобразовании.

Проверки:

- shared parser сохраняет bytes и принимает структуру при отсутствующих scenario files;
  прежние strict schema/ID/unknown/bounds тесты run_batch сохранены;
- bounded reader отклоняет directory/FIFO/empty/oversize/missing; O_NONBLOCK open предотвращает
  ожидание writer FIFO; файл ограничен64KiB, время regular-file IO deadline не ограничено;
- Qt helper: ID/порядок/relative paths, literal `#`/`%`, selected symlink batch base и
  filesystem meaning symlink/.. после преобразования URL;
- controller: импорт без endpoint, без worker/выходного report; busy import отклоняется,
  состояние текущего исследования не сбрасывается;
- standalone QML: cancel chooser сохраняет draft, успешный import заменяет весь состав,
  неверный поздний duplicate-ID row сохраняет прежний список и показывает ошибку;
  добавление после импорта выбирает свободный ID;
- live QML Unix/TCP/combined: импорт relative-path плана, удаление `retired`, добавление `case-2`,
  перестановка и запуск кнопкой; итоговые ID проверены как case-2/case-1. Существующие
  passed/failed/not_requested/incomplete и cancel/restore executions сохранены.

Targeted research_scenarios/simulator_research_unit/simulator_configuration_qml прошли3/3
за6,90с. Live client_transports прошёл за82,54с. Рисунок3.54 — реальное QML окно после
структурного импорта regression плана, до preflight/запуска; воспроизводится client_transports,
исходный screenshot `/tmp/sv-reference-result.png.import.png`. Содержимое временных сценариев
создаёт и удаляет transport harness. Это Linux/Qt6.11.2 evidence, не scientific truth и не
квалификация Qt5/Aurora/устройства. 37 PlantUML blocks затронутых архитектурной главы и главы3
прошли -checkonly; clang-format и git diff --check прошли.

Импорт не пишет исходный план, не создаёт переносимый bundle и не переключает server recording.
Предыдущий draft сохраняется при ошибке; данные/полный preflight заново требуются при запуске.
Сохранение неисполненного черновика, comparison разных reports и прежние исследовательские/
целевые gates остаются открытыми.


Итоговый полный последовательный Linux прогон с доступом к локальным сокетам:
**CTest68/68 за159,29с**, client_transports82,54с, simulator_configuration_qml6,87с.
После исправления file URL regression все input/restore/quality и QML проверки прошли;
изменение алгоритмов сшивки либо новое научное подтверждение этим срезом не заявляются.
