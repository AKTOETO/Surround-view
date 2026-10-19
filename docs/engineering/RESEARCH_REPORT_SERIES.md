# Сопоставление серии исследовательских отчётов

В **Исследование → Сравнение** выберите **Серия 2–8 отчётов**. Добавьте individual
`ExperimentRunner` reports кнопкой; `batch.json` и audit summaries не подходят.
Первый файл — базовый, стрелки меняют порядок. Нажмите **Сравнить серию**.
Соединение с сервером не нужно. Загруженный baseline указан отдельно от редактируемого
списка: выбор следующей серии не переименовывает прошлый результат.

## Показатели и допуск

Сервис строит сравнение baseline с каждым последующим report общим
`compare_reports` ([[engineering/RESEARCH_REPORTS]]), сейчас policy
`report_claims_comparison_v4`. Settings matching, input/reference/model hashes,
completeness, observation workload и отдельные server/client build gates сохраняются.
Никакой новый упрощённый путь допуска не добавлен.

Для каждого **варианта baseline** показаны minimum / median / maximum:

| Показатель | Какие значения входят | Условие доступности |
|---|---|---|
| Render p50, ms | p50 render_wall каждого report | Valid timing delta во всех парах и baseline self-check |
| MAE RGB8 | Pixel-weighted MAE каждого report | Valid MAE delta во всех парах и baseline self-check |
| RMSE RGB8 | Pixel-weighted RMSE каждого report | Valid RMSE delta во всех парах и baseline self-check |

Каждая метрика показывает `eligible_reports / required_reports`. Если прошли не все,
все три aggregate values равны null и отображаются `—`; из сводки не исключаются
неудобные отчёты. Например, отсутствие client build в одном report блокирует timing
aggregate, но не обязательно quality aggregate. Отсутствие метода в одном report
блокирует его aggregate; изменение полного trial plan может блокировать timing и для
других методов. Raw metrics и конкретные причины остаются в попарных audit results.

Это **статистика report summaries**. Median render p50 — медиана нескольких уже
вычисленных p50, не p50 объединённых кадров. Не объединяются raw frame samples,
quality pixels или времена разных сцен. CI, статистическая значимость, независимость
повторов, scientific acceptance, ranking и sustained FPS не выводятся.
Разные profiles, присутствующие только вне baseline, сохраняются как right_only в
попарном audit, но не становятся строками aggregate table. Смените baseline, если
нужно исследовать эти варианты.

## CLI и общий C++ service

```sh
build/svctl research-compare-series artifacts/series-audit.json \
  artifacts/run-a/report.json artifacts/run-b/report.json artifacts/run-c/report.json
```

Это offline-команда: выбранный Unix/TCP endpoint не используется. Сначала читаются и
проверяются все inputs, затем создаётся **новый** audit file через `ReportOutput/O_EXCL`.
Existing file, hard link или symlink не заменяются. При ошибке записи может остаться
неполный новый файл; atomic publication/fsync не реализованы. Ошибка input/duplicate
не создаёт output. CLI печатает audit JSON; exit0 означает успешное построение сводки,
а не прохождение quality criteria. Ошибка setup/input/output — exit3; arity — exit2.

`report_series.hpp` предоставляет `compare_report_series(vector<object>, cancel)` и
`compare_report_series_files(vector<path>, cancel)`. В C++ также нет доступа к серверу,
автоматического запуска Python или открытия вложенных image/reference paths.

## Artifact и границы

Export schema1 имеет policy `report_series_comparison_v1`:

- `baseline_index=0`, `report_count`, `profile_scope=baseline_variants_only`;
- `pair_policy`, `aggregation=min_median_max_of_report_summaries_v1`;
- `comparisons[]`: индекс небазового report и полный попарный result с passports,
  settings, raw metrics, deltas, gates/reasons;
- `rows[]`: baseline variant index/settings, три раздельных агрегата с eligibility count;
- file API также сохраняет `inputs[]` с index/path/точным raw SHA256/bytes и `input_bytes`.

Сводку нельзя подать как individual source report в повторное сравнение. Hashes —
заявленная идентичность прочитанных bytes, не подпись/attestation исходных кадров.
Export содержит локальные пути. Два одинаковых parsed documents отвергаются, включая
различия только порядка object keys/пробелов и file aliases. Изменённые timestamps/labels
могут различать копии: эта защита не устанавливает независимость запусков.

Пределы файлового API:2–8 reports, ≤16MiB каждый и ≤64MiB суммарных raw bytes.
Читаются только regular files через bounded nonblocking reader; FIFO/устройства/каталоги
отклоняются. Лимит фактического чтения проверяется по мере загрузки. JSON DOM и audit
results требуют дополнительной памяти: это не RSS budget. Core memory API не является
file sandbox и получает уже созданные вызывающим кодом objects.

Cancel проверяется между reads/этапами/pairs/profiles; parse и один recursive comparison
не прерываются внутри операции. Hard I/O deadline нет. Qt использует прежний single worker,
busy/cancel/close guard; пара и серия не запускаются параллельно. Ошибка/отмена очищает
прошлые results; при смене режима pair/series не показывают result другого типа.
Save создаёт новый audit, не меняет исходные отчёты. Qt6/Linux проверен; Aurora/Qt5 и
полноценные статистические методы подтверждающей серии остаются открытыми.

Схема: [[architecture/RESEARCH_RUNTIME#Описательная сводка нескольких отчётов]],
проверки: [[validation/RESEARCH_RUNTIME]], диплом §3.74.
