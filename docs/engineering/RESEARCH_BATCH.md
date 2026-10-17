# Последовательный запуск исследований в C++

`svctl research-batch` запускает несколько сценариев через общий `sv-research` и
`sv-client-lib`. Сервер исполняет алгоритмы; клиентский application service фиксирует план,
собирает результаты и оценивает внешний эталон. Python dispatcher не используется.
Это первый срез supervisor: один уже запущенный сервер и одна подключённая replay-запись.
Он не запускает серверные процессы, не меняет recording, не создаёт Blender-изображения.

## Запуск и формат плана

Сервер должен работать с replay manifest, содержащим достаточно кадров для каждого сценария.
Пример после обычного запуска сервера из [[engineering/USAGE]]:

```sh
build/svctl --unix /tmp/sv-prototype --timeout-ms 5000 \
  research-batch configs/research/batch-repeatability.json artifacts/batch-repeatability-01
```

Для удалённого сервера замените `--unix DIR` на `--tcp HOST CONTROL_PORT DATA_PORT`.
Родитель output должен существовать, а сам каталог — отсутствовать. Каждый новый запуск
использует новое имя. Нельзя выбирать существующий пустой каталог или symlink.

```json
{
  "schema_version": 1,
  "runs": [
    {"id": "sequence-a", "scenario": "multiband-matrix.json"},
    {"id": "sequence-b", "scenario": "multiband-matrix.json"}
  ]
}
```

Относительный `scenario` разрешается от каталога batch plan. Относительный `quality_reference`
внутри сценария — от каталога самого сценария. Output paths не задаются участниками:
они выводятся из безопасного ID. Допустимы ASCII буквы/цифры/`_`/`-`, длина 1–64;
ID уникальны, `batch` зарезервирован для summary. Неизвестные поля отвергаются.

Пределы: plan 64 KiB, 1–8 участников, scenario до 1 MiB, суммарно до 10000 trials
с warmup и до 128 MiB удерживаемых RGB/mask pixels. Во время загрузки очередного эталона
до проверки суммарного бюджета возможно временное превышение последнего предела;
это не предел общей RSS. Все сценарии обязаны явно задавать `reset_replay: true`.
Формат сценария/порядок trials — [[architecture/RESEARCH_RUNTIME]], эталонов —
[[engineering/RESEARCH_QUALITY]]. Пример выше повторяет одну матрицу для проверки
воспроизводимости; второй запуск не является независимой scene или holdout.

## Предварительная проверка и исполнение

До любого подключения загружаются и проверяются **все** сценарии и эталоны, включая hashes
пикселей и guards. Они удерживаются неизменяемыми в памяти. Затем создаётся новый output,
эксклюзивно резервируются summary, все отчёты и capture directories. Ошибка позднего
участника или output preparation не должна запускать ранних участников.

После подготовки участники исполняются в порядке плана. Каждый получает отдельную
клиентскую сессию и lease, выполняет reset, свою матрицу и restore fusion/surface/pause.
Следующий начинается только после завершения предыдущего. Семантика restore ограничена:
старый replay cursor, OS/GPU caches и серверный процесс не восстанавливаются.

Lease не удерживается между участниками; другие клиенты могут менять состояние в этом
интервале. Серия не является глобальной атомарной транзакцией, и endpoint сам по себе не
доказывает неизменность процесса/записи. Фактические input/model/view identities и состояния
нужно проверять по каждому отчёту; общая process/dataset registry остаётся отдельной задачей.

Если workflow или restore провалился, либо evaluator обнаружил несовпадение guards,
оставшиеся участники не запускаются. Если workflow успешен, но заранее заданный quality
threshold не пройден, остальные участники продолжают исполняться: это научный результат,
а не технический отказ. Флаг отмены API проверяется между участниками и внутри общего runner.
Отмена активного прогона проходит его обычный путь restore. CLI пока не предоставляет
обработчик Ctrl+C через этот флаг; завершение процесса не заменяет штатную отмену.

## Результаты и коды завершения

В новом каталоге находятся `batch.json`, `ID.json` и при capture — `ID.json.frames/*.rgba`.
Ссылки `rgba_file` разрешаются от каталога **индивидуального** отчёта.
У каждого отчёта есть `scenario_input_sha256`: hash исходных байтов сценария при загрузке.
Он отличается от существующего `scenario_sha256`, который описывает нормализованную
исполняемую конфигурацию. Summary содержит raw plan hash, семантический plan snapshot,
сценарные hashes, результаты каждого участника и ссылки на отчёты.

| Поле summary | Значение |
|---|---|
| `scope` | `same_server_recording_v1`: текущий ограниченный workflow |
| `status` | `complete`, `failed` или `cancelled` |
| `completed` | Успешно завершённые workflow с подтверждённым restore, включая quality rejection |
| `reported` | Участники, вернувшие и записавшие отчёт, включая технически неуспешные |
| `runs[].status` | `not_started`, `reported` либо `error` при исключении без возвращённого отчёта |
| `success` | Все workflow выполнены; само по себе не означает принятое качество |
| `restored` | Подтверждённый restore; при отмене до первого подключения — true, изменений не было |

Policy `all_defined_member_criteria_v1` учитывает заданные критерии участников:
`failed`, если хотя бы один завершённый участник не прошёл пороги; `passed`, если все
заданные критерии пройдены; `not_requested`, если критерии нигде не заданы. Участники без
критериев не становятся научно принятыми из-за aggregate `passed`. Неполная/неуспешная серия
всегда даёт `incomplete`. Counts `reported_criteria_members`/`failed_criteria_members`
относятся к уже записанным отчётам, не к ещё не запущенным участникам.

CLI выводит JSON со ссылкой на `batch.json`; exit 0 — успешный workflow с passed/not_requested,
5 — полностью исполненная серия с quality rejection, 4 — failed/incomplete/cancelled,
3 — исключение подготовки/IO. Аргументы неправильной формы дают 2.

Отчёты резервируются заранее и не перезаписываются. Не запущенные участники оставляют
пустые резервные файлы. Ошибка подготовки может оставить частичный каталог; используйте
новый output, не принимайте существование файла за завершённый опыт. Summary записывается
только в конце; durable checkpoints, crash recovery и resume ещё не реализованы.

## API и проверки

`FileExperiment` использует PImpl и отделяет загрузку immutable inputs от резервирования output
и одноразового исполнения. `run_batch(plan, directory, options, cancel, progress)` предоставляет
общий blocking service; progress передаёт `(member_index, completed_trials, total_trials)`.
Для GUI его нужно вызвать вне UI thread и перенести callback в GUI executor. Первый GUI
одного исследования реализован ниже; редактор batch-планов ещё отсутствует. Это application service над `sv-client-lib`, а не новая серверная команда.

Проверки: `research_batch_tests.cpp` — предварительная проверка, bounds, отмена до сети,
immutable сценарий, single-attempt и защита output aliases; `test_client_transport.py` —
настоящие Unix/TCP сессии, воспроизводимость RGBA между участниками, quality rejection с
продолжением и source-content mismatch с остановкой/restore. Сводка — [[validation/RESEARCH_RUNTIME]].

## Исследование из sv-simulator

Запустите replay server обычной командой из [[engineering/USAGE]], затем клиент:

```sh
build/examples/sv-simulator/sv-simulator --unix /tmp/sv-prototype
# Для удалённого сервера:
build/examples/sv-simulator/sv-simulator --tcp HOST CONTROL_PORT DATA_PORT
```

1. Дождитесь подключения и завершения текущих configuration requests. Откройте **Исследования**
   в шапке окна. Настройте нужную поверхность и виртуальный ракурс заранее на основной странице.
2. На вкладке **План** отметьте методы, задайте frames/warmup/repeats/seed, pyramid levels,
   boundary и seam solver. `Сравнить обе` добавляет zero/normalized варианты только для двух
   multiband методов; не создаёт бессмысленные дубликаты остальных. Solver применяется только
   к graph-cut методам. Список методов пока фиксирован: несовместимый сервер отклонит запрос.
3. Укажите **новый** каталог результата с существующим родителем. Кнопка **Новое имя** предлагает
   путь `artifacts/gui-research-UTC-COUNTER` относительно рабочего каталога приложения. Число кадров
   не определяется автоматически: оно должно помещаться в подключённую запись. По умолчанию — 2.
4. Нажмите **Начать исследование**. Worker сохранит `scenario.json`, зарезервирует `report.json`
   и `report.json.frames`, затем вызовет тот же FileExperiment, что CLI. Вкладка **Выполнение**
   показывает trials, включая warmup; 100% ещё не означает успешный restore.
5. Кнопка **Отменить и восстановить настройки** запрашивает штатную отмену. Она не прерывает
   синхронную операцию посередине. Закрытие окна во время прогона также запрашивает отмену:
   окно закроется после возврата worker, в том числе при сообщении о неудачном restore.
6. На вкладке **Результаты** доступны render-wall p50/p95 и measured count каждого варианта,
   terminal status и копируемый путь полного отчёта. Прочерки означают отсутствие метрики,
   а не нулевую задержку или идеальное качество. Форма пока не задаёт внешний reference:
   MAE/PSNR и scientific quality acceptance здесь не оцениваются. Для них используйте CLI и
   reference schema2 из [[engineering/RESEARCH_QUALITY]].

UI не редактирует конфиг сервера: временные переключения и restore выполняет сервер через
sv-client-lib. Ракурс/поверхность сохраняют текущую постановку; прежний replay cursor и caches
не возвращаются. Во время прогона основные controls заблокированы, после завершения запрашивается
обновление active configuration/revision; редактируемый draft не применяется автоматически.
Это не защита от команд других клиентов и не whole-process isolation.

ResearchController с PImpl удерживает worker и cancellation flag. Значения формы и endpoint
фиксируются перед запуском. Потеря viewer-подключения меняет возможность нового запуска,
но не перенаправляет текущий worker: он завершает свою отдельную сессию или сообщает ошибку.
UI poll atomic counters раз в 100 мс; не создаётся queued event на каждый trial. Terminal callback
переносит только строки по вариантам, не полный report. Destructor запрашивает отмену и join;
при зависшем OS/driver IO общая длительность shutdown всё ещё зависит от текущей операции.

Viewer остаётся дополнительным потребителем кадров; GUI timing samples нельзя автоматически
приравнивать к headless qualification/FPS. Warmup исключён из timing summaries, повторы одного
кадра не становятся независимыми scene samples. GUI batch editor, импорт/export произвольных
матриц, reference chooser, сравнение разных reports и durable resume остаются открытыми.

Тесты: `simulator_research_tests.cpp` проверяет typed draft, асинхронный отказ подготовки,
недоступные метрики, отзывчивость UI event loop, frozen endpoint, cancel/restore и destructor/join.
Последние три cases запускаются transport harness на настоящем сервере для Unix/TCP/combined.
`simulator_research_qml_tests.cpp` меняет формы/вкладки и проверяет отсутствие QML warnings;
его screenshot — UI fixture плана, не научный результат. Схема: рисунок RR.7
[[architecture/RESEARCH_RUNTIME#GUI одного исследования: граница worker и UI]].
Отдельный ResearchCloseGuard используется самим main.qml; QML test проверяет отклонение первого
close при busy, cancel, последующий close после terminal callback и сброс closeWhenDone.
Этот window-тест использует отказ подготовки без сети; настоящий restore проверяется отдельно
live controller cases. Это разные проверяемые границы одного workflow.
