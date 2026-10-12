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

§4.50 —post-hoc semantic/boundary stratification360 outputs: [[validation/SPATIAL_ROI]]. Рисунки4.57–4.58 воспроизводятся без server rendering:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/spatial_roi_v1.json --inputs-root artifacts/seam-generalization-v1
```

§4.51 — native raster coverage пола/оболочки и ограничения прежних ракурсов: [[validation/CARRIER_COVERAGE]]. Рисунок4.59:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/carrier_coverage_v1.json --captures-root artifacts/carrier-coverage-v1
```

§4.52 — lateral paired RGB/common floor-shell исследование,120 условий: [[validation/CARRIER_LATERAL]]. Рисунки4.61–4.62:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/carrier_lateral_v1.json --captures-root artifacts/carrier-lateral-v1 --inputs-root artifacts/carrier-lateral-inputs-v1
```

§4.53 — controlled height/range,240 native условий, отрицательные результаты и visibility/clipping ограничения: [[validation/PARALLAX_HEIGHT]]. Рисунки4.64–4.65:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/parallax_height_v1.json --captures-root artifacts/parallax-height-v1 --inputs-root artifacts/parallax-height-inputs-v1
```

§4.54 — conditional visible-surface оценка240 исторических outputs: [[validation/PARALLAX_VISIBILITY]]. Рисунки4.67–4.68:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/parallax_visibility_v1.json --captures-root artifacts/parallax-height-v1 --inputs-root artifacts/parallax-height-inputs-v1
```

§4.55 — fully framed object/two-mount screen,480 новых native условий: [[validation/PARALLAX_FRAMED]]. Рисунки4.70–4.71:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/parallax_framed_v1.json --captures-root artifacts/parallax-framed-v2 --inputs-root artifacts/parallax-framed-inputs-v2
```

§4.56 — парный cubemap sampling control,256→512 при фиксированных fisheye400/output320×180. Генератор рисунков4.73–4.74:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/parallax_sampling_v1.json --captures-root artifacts/parallax-sampling-v1 --inputs-root artifacts/parallax-sampling-inputs-v1 --reference-root artifacts/parallax-framed-v2
```

Нужны raw данные обеих серий; regeneration figures не запускает новые серверные trials. Протокол: [[research/PARALLAX_SAMPLING_PROTOCOL]].

§4.57 — sensitivity output320×180→640×360 при прежних512px cubemap/400px fisheye. Рисунки4.76–4.77:

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/parallax_output_v1.json --captures-root artifacts/parallax-output-v1 --inputs-root artifacts/parallax-output-inputs-v1 --reference-root artifacts/parallax-sampling-v1
```

Raw обеих серий необходимы; independent high-resolution truth не получается resize прежнего. Политики high8/high32pixels заданы заранее: [[research/PARALLAX_OUTPUT_PROTOCOL]], [[validation/PARALLAX_OUTPUT]].

§4.58 — fisheye400→800 при cubemap512/output640×360 и прежнем angular support. Рисунки4.79–4.80:

```bash
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/parallax_sensor_v1.json --captures-root artifacts/parallax-sensor-v1 --inputs-root artifacts/parallax-sensor-inputs-v1 --reference-root artifacts/parallax-output-v1
```

Нужны raw обеих серий. Все480 pairs/960 cube faces и unchanged independent truth проверены; повторный paired audit побитно совпал. План/результаты: [[research/PARALLAX_SENSOR_PROTOCOL]], [[validation/PARALLAX_SENSOR]]. Native source correspondence/triangle-hit пока остаётся следующим этапом.

§4.59 — native actual-carrier positions и независимый triangle-intersection pilot. Рисунки4.82–4.83:

```bash
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_carrier_positions.py --report docs/validation/baselines/carrier_positions_v1.json --root artifacts/carrier-inspection-v1
```

Нужны generated position exports; source script хранится рядом с главой. Full read-only audit и команды native probe/GTest: [[engineering/USAGE#Фактические координаты носителя]], [[validation/CARRIER_POSITIONS]]. Source/scene correspondence и wire channel остаются следующим этапом.

§4.60 описывает native source projection/validity capture, проверку на двух GLES backends и ограничения дальнейшего correspondence analysis; диаграмма4.84 встроена как PlantUML.

§4.61 описывает capture hybrid fusion contributions, known-answer reconstruction и диаграмму4.85; определения локальных weights и нелокальных вкладов разделены.

§4.62 содержит первый native source-surface correspondence pilot: локализация выбора дороги вместо поднятого объекта на одном historical frame; три подписанных графика/readback comparison и PlantUML diagram4.89. Данные и ограничения: [[validation/SOURCE_CORRESPONDENCE]].

§4.63: height/second-frame source-surface controls,20 native captures, явные world/vehicle transforms, таблица outcomes и рисунки4.90–4.92. [[validation/SOURCE_CORRESPONDENCE_CONTROLS]]. Ground-contact target объёмный; planar known-answer control остаётся открытым.

§4.64 — isolated planar geometry control, analytical/Blender oracle,10 carrier/frame условий, таблица strict failures и рисунки4.93–4.95. [[validation/PLANAR_CORRESPONDENCE]].
