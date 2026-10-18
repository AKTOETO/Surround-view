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

| Условие | Правило первой реализации | При недостатке данных |
|---|---|---|
| Структура | schema1, paused frame/sequence fusion experiment; scenario parser, ≤32 variants, ≤10000 samples; уникальные frame/block/variant | Некорректный документ отклоняется |
| Соответствие вариантов | Полные наблюдённые `fusion` и `surface`, согласованные с заявленным scenario; численные значения сравниваются независимо от JSON integer/double | Только односторонняя строка либо `ambiguous`, без delta |
| Входы | READY paused replay, четыре использованные камеры; hashes decoded RGB8 и численной модели, calibration ID, timestamp, порядок кадров, view/shape/product | `different_or_unverified` |
| Полнота | success + restored, полный набор warmup/measured координат, известный quality status | Все вычисляемые deltas отключены |
| Время | Одинаковые frames/warmup/repeats/seed/reset, фактический порядок полных профилей, native trial-order algorithm и percentile definition, одинаковый source fingerprint; известные backend/vendor/renderer | Нет timing delta |
| Качество | Reference schema2 `masked_rgb8_v1`; одинаковые kind/view/shape/inputs, decoded RGB и mask hashes, число выбранных пикселей; все frames оценены | Нет quality delta |

Поддержаны reference kinds `regression` и `independent_ground_truth`; они не смешиваются.
Dataset ID и путь файла не заменяют content identity. Reference path, capture path и
остальные вложенные пути **не открываются**, изображения повторно не декодируются.
Hashes в отчёте являются заявлениями автора: импорт не аттестует исходные данные,
аппаратную платформу или независимость truth. Разные GPU/backend разрешены для будущего
сравнения PC/целевой платформы; timing gate требует совпадения source fingerprint и измеряемой работы,
но не доказывает сопоставимость OS/CPU/частот/нагрузки. Source fingerprint описывает исходники, а не полный бинарный build: compiler/options,
SDK и зависимые библиотеки этим gate не аттестуются. Для этого нужен отдельный
[[engineering/PLATFORM_TEST|паспорт стенда]].

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
`report_claims_comparison_v1`, путями A/B, SHA256 прочитанных raw bytes, паспортами,
строками/соответствиями, gates/reasons и ограничением интерпретации. Это audit summary,
не новый ExperimentRunner report; повторно сравнивать его как исходный report нельзя.
`ReportOutput` использует `O_EXCL`: существующий файл/ссылку не заменяет. При ошибке
записи может остаться неполный новый файл; fsync/durable journal не реализован.
Экспорт содержит локальные пути: перед передачей третьим лицам учитывайте их содержимое.

Сравнение более двух отчётов, confidence intervals, переносимый bundle с изображениями,
revalidation hashes по первичным данным и целевая квалификация остаются в [[planning/ROADMAP]].
Диаграмма процесса: [[architecture/RESEARCH_RUNTIME#Сопоставление двух исследовательских отчётов]].
