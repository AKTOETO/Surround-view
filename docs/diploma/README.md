# Текст диссертации

Тема: **«Исследование и разработка системы отображения и методик калибровки функции интерактивного кругового обзора ТС для ОС Аврора»**.

Здесь находится связный текст глав. [[planning/THESIS|План диссертации]] задаёт структуру, [[research/TOPIC|постановка]] — границы работы, [[references/README|каталог источников]] — единственные записи внешних материалов. Требования и технические контракты остаются в своих тематических каталогах.

С 06.10.2026 в обзор добавлено отдельное исследование [[research/PROJECTION_AND_STITCHING|методов слияния четырёх камер и геометрий-носителей]]. Глава 1 развивает теорию, глава 2 фиксирует варианты, глава 4 содержит screening и численные подготовительные исследования. Сравнительный quality benchmark методов с единым ground truth и бюджетом ещё не выполнен.

## Главы

| Глава | Файл | Состояние |
|---|---|---|
| 1. Теоретические основы построения интерактивных систем кругового обзора | [[diploma/01_THEORETICAL_FOUNDATIONS|Первая глава]] | Рабочая редакция разделов 1.1–1.8; это теоретическая глава, не отчёт об опытах |
| 2. Анализ требований и выбор методов | [[diploma/02_REQUIREMENTS_AND_METHODS|Вторая глава]] | Сравнения, калибровка, архитектура и программа опытов |
| 3. Разработка системы | [[diploma/03_PROTOTYPE_IMPLEMENTATION|Третья глава]] | Реализация Linux-профиля, OpenCV, native suite, RPM и 3D стенда |
| 4. Экспериментальное исследование | [[diploma/04_EXPERIMENTAL_STUDY|Четвёртая глава]] | Измерения Mesa/RTX, калибровочные серии, Blender и screening; полный quality benchmark ещё не выполнен |
| Заключение | [[diploma/05_CONCLUSION|Предварительное заключение]] | Трассировка требований; окончательный вывод обновить после оставшихся опытов и целевой проверки |

Все главы — рабочий текст, требующий согласования и редакторской проверки. Глава 4 сообщает только выполненные опыты. Устройство Авроры, реальные шаблонные снимки и основная длительная серия пока отсутствуют; соответствующие результаты не подставляются из настольного стенда.

## Иллюстрации и данные

[plot_experiments.py](plot_experiments.py) находится рядом с главой 4. Он читает первичные значения из [[prototype/MEASUREMENTS]], создаёт SVG времени рендера и ошибки калибровки в `figures/experiments/`. Для парного изображения плоскости/bowl используются реальные PPM-выходы серии. Команды повторения — [[prototype/README#Повторение опытов и построение графиков]]. Изображения не дублируют отдельные заметки или машинные отчёты.

[plot_blender.py](plot_blender.py) формирует иллюстрации истинной Blender-сцены, четырёх fisheye-входов и GLES surround readbacks для главы 3. Команды — [[engineering/BLENDER]], результаты короткой проверки — [[validation/BLENDER_SMOKE]].

## Оформление

- Одна глава — один Markdown-файл со стабильным ASCII-именем и полным русским названием в заголовке.
- Формулы: `$...$` и `$$...$$`, совместимые с MathJax в Obsidian. Уравнения имеют номера внутри главы.
- Схемы: ограждённые блоки `plantuml` с `@startuml` и `@enduml`. В первой главе три схемы; растровые копии не дублируют их исходники.
- Внешние ссылки `[Sxx]` определяются в конце главы, записи каталога связаны через wikilinks. При экспорте требуется преобразовать wikilinks средствами выбранного экспортёра.
- Если график создаётся Python-скриптом, скрипт хранится рядом с главой, изображения — в `figures/`; подпись указывает, является ли график теоретическим расчётом или экспериментом. Первичные числа и условия опыта публикуются в Markdown, большие исходные данные — в игнорируемом `artifacts/`.
- Теоретические положения, проектные следствия и полученные результаты различаются в тексте. Порог или гипотеза не становятся результатом без протокола проверки.

## Иллюстрации системы и baseline 0.2.0

[plot_system.py](plot_system.py) сохраняет схемы и расчётные графики для глав 1–3 и заключения. Для трёх фотографических readback-кадров он вызывает реальные `sv-scene`/`sv-bench`, сохраняет сырые результаты в artifacts и только картинки — в документации. Источник CC0 и ограничения — [[engineering/SCENE]].

```sh
python3 docs/diploma/plot_system.py
python3 docs/diploma/plot_system.py --capture-street --work artifacts/street-figures-new
python3 docs/diploma/plot_platform.py
```

[plot_platform.py](plot_platform.py) читает [[validation/baselines/PC_RTX]] и [[validation/baselines/PC_MESA]], предварительно проверяет сопоставимость, строит графики медиан p95 render/readback и draw с диапазоном повторов, а также матрицу критериев. GPU не нужен для построения графиков из сохранённых отчётов. Это отдельная версия эксперимента, не перезапись исторических данных 0.1.0.

`figures/` разделён на `theory/`, `design/`, `implementation/`, `experiments/`, `conclusion/`. Каждая картинка встроена в соответствующую главу с номером и подписью; в ней различаются аналитический расчёт, модельная схема и реальное наблюдение программы. PlantUML остаётся прямо в Markdown. Инструкция по сборке/использованию/упаковке: [[engineering/BUILD]], [[engineering/USAGE]], [[engineering/AURORA]].

Вернуться к [[README|карте документации]].

## Дополнение 0.6.0

Глава 3 (§3.16) описывает source workers, per-camera queues и разделение producer/client endpoints; рисунок 3.14 — actual server output новой socket pipeline. `plot_blender.py` принимает дополнительный `--socket-validation artifacts/blender-virtual-v06`, остальные параметры сохранены. Глава 4 (§4.9.4/§4.14) содержит новые PC baselines и source tests; старые измерения не переписаны. Происхождение — [[validation/SOURCES_SMOKE]].

## Native boundary follow-up, 11.10.2026

§3.31 описывает последовательный C++ runner/capture, §4.45 — 76 production-server условий и отрицательные результаты normalized. Исходные численные reports: [[validation/SERVER_BOUNDARY]]. Графики из сохранённого baseline и local captures:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py
```

Для actual views требуются сохранённые RGBA в `artifacts/server-boundary-v2`; воспроизведение их нативным runner описано в [[engineering/USAGE#Многокадровый native эксперимент и RGBA capture]]. Две позы и все четыре profiles показаны без выбора лучшего изображения по результату.

§3.32/§4.46 добавляют native multilabel solver, exhaustive global-gap counterexample и первый actual server screen: [[validation/MULTILABEL_SEAM]]. Рисунки воспроизводятся тем же скриптом:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/server_seam_v1.json --captures-root artifacts/server-seam-v3 --optimizer-report docs/validation/baselines/seam_optimizer_v1.json
```

§4.47 — frozen follow-up на трёх новых экземплярах улицы и отклонениях монтажа,24 server conditions. Рецепт и ограничения: [[research/SEAM_GENERALIZATION_PROTOCOL]], результаты: [[validation/MULTILABEL_SEAM#Новые сцены и отклонения монтажа]]. Воспроизведение рисунков4.50–4.51 после capture/native run:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/seam_generalization_v1.json --captures-root artifacts/seam-generalization-v1/seed101-run --fixture artifacts/seam-generalization-v1/seed101-inputs --capture artifacts/seam-generalization-v1/seed101-capture
```

§4.48 —120 native условий сравнения пяти носителей с сопоставимым числом треугольников и общими mesh-buffer ceilings: [[validation/CARRIER_BUDGET]]. Рисунки4.52–4.54:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/carrier_budget_v1.json --captures-root artifacts/carrier-budget-v1 --fixture artifacts/seam-generalization-v1/seed101-inputs --capture artifacts/seam-generalization-v1/seed101-capture
```

§4.49 — finite coarse/medium/fine sensitivity,360 условий/240 новых, independent truth и output difference разделены: [[validation/CARRIER_REFINEMENT]]. Рисунки4.55–4.56:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/carrier_refinement_v1.json --captures-root artifacts/carrier-refinement-v1 --fixture artifacts/seam-generalization-v1/seed101-inputs --capture artifacts/seam-generalization-v1/seed101-capture
```
