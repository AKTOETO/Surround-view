# Сопоставление двух исследовательских отчётов

В `sv-simulator` откройте **Исследование → Сравнение**. Выберите A и B кнопками,
затем нажмите **Сравнить отчёты**. Соединение с сервером не требуется. Входы — два
individual `ExperimentRunner` report schema1: `report.json` отдельного опыта либо
`ID.json` внутри результата batch. `batch.json` — индекс серии, его выбирать нельзя.
Получение исходных отчётов: [[engineering/RESEARCH_BATCH]],
[[engineering/RESEARCH_QUALITY]].

## Что проверяется

Общий C++ service `examples/common/research/report_comparison.hpp` предоставляет
`compare_reports` для документов и `compare_report_files` для файлов. GUI использует
его через отдельный `ResearchComparison` с PImpl. Результаты последнего серверного
прогона и локального сравнения хранятся раздельно. Это операция application service;
новой команды SV01, lease либо изменения server config нет.

| Условие | Текущее правило | При недостатке данных |
|---|---|---|
| Структура | schema1, paused frame/sequence fusion experiment; scenario parser, ≤32 variants, ≤10000 samples; уникальные frame/block/variant | Некорректный документ отклоняется |
| Соответствие вариантов | Полные наблюдённые `fusion` и `surface`, согласованные с заявленным scenario; численные значения сравниваются независимо от JSON integer/double | Только односторонняя строка либо `ambiguous`, без delta |
| Входы | READY paused replay, четыре использованные камеры; hashes decoded RGB8 и численной модели, calibration ID, timestamp, порядок кадров, view/shape/product | `different_or_unverified` |
| Полнота | success + restored, полный набор warmup/measured координат, известный quality status | Все вычисляемые deltas отключены |
| Время | Одинаковые frames/warmup/repeats/seed/reset/capture, подтверждённый observation profile, фактический порядок полных профилей, native trial-order algorithm и percentile definition, одинаковые source fingerprint и проверенный configured-build hash; известные backend/vendor/renderer | Нет timing delta |
| Качество | Reference schema2 `masked_rgb8_v1`; одинаковые kind/view/shape/inputs, decoded RGB и mask hashes, число выбранных пикселей; все frames оценены | Нет quality delta |

Поддержаны reference kinds `regression` и `independent_ground_truth`; они не смешиваются.
Dataset ID и путь файла не заменяют content identity. Reference path, capture path и
остальные вложенные пути **не открываются**, изображения повторно не декодируются.
Hashes в отчёте являются заявлениями автора: импорт не аттестует исходные данные,
аппаратную платформу или независимость truth. Разные GPU/backend не запрещают чтение отчётов. Policy v3 требует совпадения
проверенных configured-build claims для автоматической timing delta. При разных
сборках PC/Aurora исходные p50/p95 остаются видны, но автоматическая разница выключена.
Сопоставимость OS/CPU/частот/нагрузки требует отдельного
[[engineering/PLATFORM_TEST|паспорта стенда]] и квалифицированной методики.

## Интерфейс и значения

Отдельно показаны workflow success, restore и quality status каждого запуска,
reference kind/dataset, backend/vendor/renderer и source revision/fingerprint.
Каждая строка показывает p50/p95 `render_wall`, measured count, MAE/RMSE/PSNR и
число quality frames. Полные применённые настройки раскрываются флажком.
Перестановка variants между файлами сохраняет соответствие по settings. При дубликатах
service не выбирает произвольного партнёра.

Разница **B−A** доступна для render p50 и MAE/RMSE только при выполнении соответствующих
gates и полноте счётчиков конкретной строки. `—` означает отсутствие допустимого значения,
а не ноль. Малые значения выводятся с шестью значащими цифрами; exact match имеет
`PSNR=∞`, legacy report без PSNR остаётся без этой метрики. Универсальный победитель,
FPS потока, статистическая значимость и причинная интерпретация времени не вычисляются.
Failed quality acceptance видна отдельно: успешное исполнение не превращает её в passed.

## Чтение, отмена и экспорт

Два обычных локальных файла читаются последовательно, каждый до **16 MiB**. Общий
`regular_input` также используется batch parser с его прежним пределом64KiB.
`O_NONBLOCK` + `fstat` отвергают FIFO/каталоги/устройства; чтение проверяет предел
фактически полученных bytes, а не только исходный stat. Между chunks64KiB и этапами
анализа проверяется cancel. Это предел входных bytes, не предел RSS JSON parser.
Нет hard I/O deadline; отмена не прерывает уже выполняющийся системный read либо parse.

Файловая пара фиксируется при старте worker; загруженные пути показаны отдельно от
выбора следующей пары, чтобы изменение chooser не переименовывало прежний результат. Новый запуск сразу очищает прошлое сравнение;
ошибка/отмена не оставляет старых строк. Закрытие окна запрашивает cancel и ждёт queued
завершения без блокировки GUI event loop. Параллельный исследовательский прогон блокируется
элементами GUI; модель сравнения сама не имеет доступа к серверу.

**Сохранить сравнение…** создаёт новый JSON schema1 с policy
`report_claims_comparison_v3`, путями A/B, SHA256 прочитанных raw bytes, паспортами,
строками/соответствиями, gates/reasons и ограничением интерпретации. Это audit summary,
не новый ExperimentRunner report; повторно сравнивать его как исходный report нельзя.
`ReportOutput` использует `O_EXCL`: существующий файл/ссылку не заменяет. При ошибке
записи может остаться неполный новый файл; fsync/durable journal не реализован.
Экспорт содержит локальные пути: перед передачей третьим лицам учитывайте их содержимое.

Сравнение более двух отчётов, confidence intervals, переносимый bundle с изображениями,
revalidation hashes по первичным данным и целевая квалификация остаются в [[planning/ROADMAP]].
Диаграмма процесса: [[architecture/RESEARCH_RUNTIME#Сопоставление двух исследовательских отчётов]].


Паспорта также показывают effective `criteria` и `criteria_source`; для прежних reports
без origin используется `legacy_unverified`. При различии наборов/источников в reasons
добавляется `acceptance_policy_differs`. Raw MAE/RMSE deltas зависят от одинаковых truth/inputs,
а не от одинаковых thresholds; это не эквивалентность verdict.
[[engineering/RESEARCH_QUALITY#Критерии конкретного сценария без изменения эталона]].


## Нагрузка наблюдения и policy v2

Исправлен допуск сравнения времени: совпадения inputs, trial plan и source fingerprint
недостаточно, если между render samples выполнялась разная работа. Новые отчёты содержат
`observation_policy.policy=paused_observation_v1`:

| Поле | Что фиксирует |
|---|---|
| frame_digest | SHA256 RGBA каждого trial, включая warmup |
| capture_frames / capture_consumer | Наличие файлового сохранения; FileExperiment помечает известный consumer `rgba8_files_v1` |
| quality_evaluation / quality_algorithm | Включение masked_rgb8_v1 evaluator для измерительных samples |
| progress_callback | Наличие callback прогресса; реализация callback не аттестуется |
| telemetry_before / telemetry_after | Нормализованные enabled/period_ms/history_capacity сервера внутри lease |
| telemetry_stable | До/после прочитаны и совпадают |

Runner читает telemetry через существующий config_read, после получения lease и после
последнего trial перед restore. Он не меняет telemetry и не записывает конфиг.
Недоступный capability/read/parse не подменяется значением false: остаётся null,
при ошибке чтения добавляется `observation_error`, а эксперимент может продолжиться.
Такой отчёт не разрешает timing delta. Два снимка не являются непрерывной аттестацией;
защита lease запрещает обычный config apply во время исследования.

Произвольный capture callback общего `run` помечается `callback_unverified`.
Совпадения двух таких строк недостаточно: без известного файлового consumer timing delta
также недоступна. Capture flag должен быть boolean и совпадать с scenario.capture_frames.
При отсутствии capture допустим только consumer none. Неизвестная версия/лишние поля,
невалидные пределы telemetry или нестабильность делают policy неподтверждённой.

Сравнение v2 добавляет `observation_gate`. Для timing delta необходимы совпадающие
подтверждённые policies, включая progress flag и все три telemetry settings. Если evaluator
включён, нужны также одинаковые schema2 content-bound reference и effective criteria:
разное число/набор проверок и разные reference могут менять работу evaluator между samples.
Разный criteria_source при одинаковых criteria сам по себе не меняет работу evaluator,
но по-прежнему отображается как acceptance_policy_differs.

При несовпадении добавляются причины observation_workload_differs либо
quality_evaluation_reference_or_criteria_differ; при пропущенной/непроверенной policy —
observation_policy_missing_or_unverified. Старые native schema1 reports читаются: исходные
p50/p95 остаются видны, quality deltas возможны по прежним content/reference guards,
но timing delta становится `—`. Качество не превращается в passed из-за доступности timing.
Изменение reference mask теперь блокирует и timing delta, если evaluator был включён.

Это **допуск сравнения заявленной работы**, не доказательство одинакового стенда или
статистической значимости. Configured compiler/options/SDK/dependency claims добавлены policy v3 ниже. Не аттестуются
callback implementation, client/server binaries, loaded dependencies, storage/cache/CPU/GPU clocks/thermal, trace I/O
и сторонняя нагрузка. Сам render_wall не включает полный клиентский capture/evaluator
latency; flags нужны из-за возможного влияния межкадровой работы на последующие samples.
Полный build/runtime passport и аппаратная приёмка остаются открытыми.

В GUI паспорта A/B показывают capture/evaluator/progress/telemetry, отдельно отображается
observation gate. Старый паспорт подписан как неподтверждённый; значения не рисуются нулями.
Диаграмма: [[architecture/RESEARCH_RUNTIME#Профиль наблюдения и допуск сравнения времени]].

Исходное состояние для restore теперь берётся из ACK успешного experiment_acquire,
а не отдельного state перед acquire. Поле `initial_state_origin=experiment_acquire_ack_v1`
фиксирует этот источник. Так снимок соответствует той конфигурации и pause state,
которые lease сохраняет для восстановления, без отдельного read до acquire.
Это также проверяется по active lease ID в нативном transport test.

Односессионный server не квалифицирован как multi-client. Проверка acquire snapshot
не является испытанием конкурентных клиентов; она фиксирует нужную границу снимка
для дальнейшего расширения server concurrency.


## Паспорт конфигурации сборки и policy v3

`fusion_catalog.build_passport` — дополнительное поле существующего ACK. Runner
сохраняет его в `report.catalog`, а `sv-platform-test` — в
`report.build.configured_build_passport`. Новая команда либо изменение SV01 не нужны.
GUI сравнения показывает hash, compiler/version, C++ standard, configuration, processor,
SDK, C++ flags и настроенный OpenCV. Полный descriptor остаётся в audit JSON.

Envelope имеет ровно `schema_version=1`, `policy=configured_build_v1`, `sha256`,
`descriptor`. Descriptor содержит source version/revision/fingerprint; compiler ID/version/
target/C++ standard и SHA256 основного toolchain file; target OS/version/processor/sysroot/
объявленный SDK; configuration/generator, global+configuration C++ и executable linker flags,
directory compile/link options, root IPO/PIC declarations; configured OpenCV/EGL/GLES versions,
способ provision GLM/OpenCV и feature flags. Header facts добавляют фактические `__cplusplus`,
`__VERSION__`, Boost/GLM/OpenSSL, standard library version/ABI и pointer bits.

SHA256 вычисляется над `sv-configured-build-v1` + NUL + компактным UTF-8 JSON descriptor:
ключи объектов сортируются рекурсивно, порядок массивов сохраняется. Это локальное правило,
не RFC8785 и не цифровая подпись. Структура, типы, обязательные/лишние поля, глубина,
размеры и source consistency проверяются до допуска. Ограничения: descriptor ≤16KiB,
строка ≤8192 bytes, depth≤8, nodes≤512; CMake ограничивает исходный metadata JSON12KiB.

`build_gate=matching_configured_build_claims` означает совпадающие проверенные hashes.
Иначе `different_or_unverified_configured_build`, reason
`build_passport_missing_or_unverified` либо `configured_build_differs` и timing delta `—`.
Отсутствующий legacy паспорт, неизвестная policy или неверный hash не скрывают исходные
тайминги и допустимые quality deltas. Совпадение build gate само по себе недостаточно:
остальные guards входов, полноты, нагрузки наблюдения и trial plan действуют одновременно.

Это паспорт **заявленной конфигурации**, не hash исполняемого файла. Не покрыты
per-target overrides, binary/object/compiler hashes, transitive toolchain includes,
патчи зависимостей и загруженные shared libraries, runtime driver/SDK attestation,
storage/cache/нагрузка. SDK string вводится сборщиком; `unreported` не доказывает его наличие.
Hash кешируется при первом запросе, не рассчитывается на каждый кадр; его overhead отдельно
не измерен. Клиентская сборка/callback этим серверным паспортом также не аттестуются.
Диаграмма: [[architecture/RESEARCH_RUNTIME#Паспорт конфигурации сборки]], диплом §3.71.
