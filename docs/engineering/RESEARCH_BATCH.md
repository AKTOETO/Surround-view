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

1. Дождитесь подключения и завершения локальных configuration requests. Откройте **Исследования**
   в шапке окна. Для формы настройте поверхность и виртуальный ракурс заранее на основной странице.
2. В **Источник плана** выберите форму либо сохранённый сценарий. Форма позволяет выбрать методы,
   frames/warmup/repeats/seed, boundary/levels и seam solver. Обе boundaries размножаются только
   для multiband методов, solver применяется только к graph-cut. Список методов пока фиксирован;
   несовместимые с сервером настройки приводят к отказу, а не к автоматической подмене метода.
3. Для формы можно нажать **Выбрать…** в группе **Внешний эталон**. Выбирается существующий
   reference manifest, совместимый с frames, source/model/view guards и размером output.
   **Убрать** отключает оценивание. Предпочтителен schema2 с pixel/model identities.
   Пороговые criteria читаются из manifest до запуска; эта форма их не редактирует.
4. В режиме сохранённого сценария выберите scenario JSON через файловый диалог. Файл не
   изменяется. Его reference разрешается от каталога исходного файла (после canonical resolution),
   значения формы/её reference не применяются. Все поддерживаемые runner поля сохраняются,
   включая surface, matrix, reset_replay и capture_frames. Batch plan здесь не поддерживается.
5. Укажите новый output с существующим родителем. **Новое имя** предлагает путь
   `artifacts/gui-research-UTC-COUNTER` относительно cwd приложения. Frames должны помещаться
   в подключённую запись; автоматического определения её длины пока нет. В форме default frames=2.
6. Нажмите **Начать исследование**. Worker проверит и зафиксирует сценарий и reference **до
   создания output и подключения**. Затем резервирует отчёт/captures и вызывает тот же C++
   FileExperiment, что CLI. При импорте бюджет trials появляется после preflight. 100% trials
   ещё не означает завершённый restore.
7. **Отменить и восстановить настройки** запрашивает штатную отмену. Текущая синхронная операция
   завершается, затем runner исполняет restore. Закрытие окна также запрашивает cancel и ждёт
   terminal callback; окно может закрыться и при сообщении о неудачном восстановлении.

### Результаты и смысл статусов

**Результаты** показывают terminal status, reference kind/dataset/schema, критерии отдельно от
workflow, timing rows и masked RGB8 MAE/RMSE/PSNR. Прочерк означает отсутствие данных.
Малая ненулевая ошибка выводится значащими цифрами, а не округляется в `0.000`.
Точный RGB match имеет PSNR `null` + `exact_match=true` в JSON и `∞ (точное совпадение)` в UI.

| Состояние | Что означает |
|---|---|
| passed | Все заданные критерии пройдены при завершённом workflow/restore |
| failed | Workflow/restore завершены, минимум один threshold не пройден; код 5 |
| not_requested с reference | Критерии не заданы; при успешном workflow метрики рассчитаны |
| not_requested без reference | Качество не оценивалось |
| incomplete | Заданные критерии не могут быть оценены полностью, например guard mismatch или cancel |
| not_available | Подготовка не выполнена, отчёт текущего запуска отсутствует |

Если criteria отсутствуют, acceptance остаётся not_requested даже при техническом отказе;
проверяйте workflow/restore и terminal code отдельно. Критерии проверяются на всех measured
samples, не на округлённой таблице. `regression` явно
показывается как проверка воспроизводимости; `independent_ground_truth` — заявление автора,
программа не доказывает независимость/истинность reference и обоснованность порогов.
Новая неуспешная подготовка очищает прежний badge/rows/report path, чтобы старое passed не
выглядело результатом нового запроса. Сохранённые файлы прежнего запуска при этом не удаляются.

Quality summaries используют один результат на frame_index без warmup/repeated counting.
MAE взвешена выбранными pixels, RMSE вычислена из общей MSE, PSNR — из этой MSE, не из среднего
PSNR отдельных кадров. `frames` в quality row — число уникальных оценённых кадров; `n` timings
включает measured repeats. Render-wall p50/p95 описывают paused samples, не sustained FPS.
Viewer остаётся дополнительным потребителем; GUI timing не равен headless qualification.

### Файлы и происхождение входов

Из формы сохраняется `scenario.json` с абсолютным reference locator, `report.json` и measured
`report.json.frames`. Snapshot воспроизводится через обычный `svctl research` при сохранении
исходных reference/RGB/mask файлов. Dataset не копируется и не становится переносимым bundle.
При импорте source scenario не копируется и не перезаписывается: сохраняются report и captures,
если их запросил исходный сценарий. Сохраняйте исходный scenario и его reference hierarchy.

`scenario_input_sha256` всегда хэширует точные переданные JSON bytes. Дополнительные поля:
`scenario_input_kind=file_bytes_v1` либо `memory_json_bytes_v1`, `scenario_input_base_directory`
и `scenario_input_path` для исходного файла. Для формы hash совпадает с bytes сохранённого
snapshot, включая newline; для импорта — с исходным файлом. Semantic `scenario_sha256` имеет
прежний смысл. FileExperiment принимает либо path, либо bytes + явную reference base; в обоих
случаях данные загружаются неизменяемыми до сети. Уже загруженный reference passport сохраняется
и при отмене до начала оценки, без утверждения о наличии измерений.

### Worker и проверки

Endpoint/form замораживаются перед запуском. Потеря viewer availability не перенаправляет
текущий worker на другую машину. UI poll atomic counters раз в 100 мс; terminal callback
передаёт строки вариантов, не полный report. Основные настройки/orbit блокируются на время
работы, после возврата запрашиваются active/revision. Это не изоляция от других клиентов.
Destructor выполняет cancel/join; общая длительность shutdown зависит от текущего OS/driver IO.

Проверки: native unit types/bounds/preflight/малые ошибки; live Unix/TCP/combined для импорта и
формы при passed/failed/not_requested/incomplete; настоящий QML запуск сохранённого сценария
с reference и отображением его результатов. Файловый диалог открывается/отклоняется в QML tests,
выбор не запускает исследование сам. ResearchCloseGuard откладывает close до terminal callback;
window case использует отказ подготовки без сети, настоящий restore проверяется live cases.
Трасса взаимодействий: рисунки RR.7–RR.8 в [[architecture/RESEARCH_RUNTIME]].

Остаются GUI batch/carrier/general matrices, capabilities-driven choices, редактирование
criteria с фиксированием до запуска, bundle import/export, сравнение разных reports и durable
resume. Текущий импорт одиночного scenario не закрывает multi-recording registry/switching.
