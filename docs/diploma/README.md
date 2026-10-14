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

§4.65 — paired NVIDIA/Mesa precision follow-up:10 новых native captures,7 known-answer controls, таблица исходов и рисунки4.96–4.97. [[validation/PLANAR_PRECISION]].

§4.66 — displaced source-surface attribution на двух frames/пяти carriers, таблица4.66 и рисунки4.98–4.100; foreground source occlusion, bilinear color control и сохранённые ray/RGB mismatch pixels. [[validation/DISPLACED_CORRESPONDENCE]].

§4.67 —60 matched native hybrid conditions, signed per-level capture, independent band/RGBA reconstruction, таблицы4.67–4.68 и рисунки4.101–4.104. [[validation/HYBRID_CORRESPONDENCE]].

§4.68: four-texel контроль1164 point-ray/RGB mismatches,1024 neighboring target support/140 residual; таблица4.69, рисунки4.105–4.107. [[validation/SOURCE_FOOTPRINT]].

§4.69: actual cube→fisheye RGB reconstruction256 texel с ошибкой0LSB;140 residual pixels разделены на135 cube-center support/5 area-only. Таблица4.70, рисунки4.108–4.110. [[validation/CUBE_FOOTPRINT]].

§4.70: independent Blender truth на547 новых frame/carrier coordinates/1227 condition pixels;640 exact archive anchors, local texel/nonlocal contribution analysis. Таблица4.71, рисунки4.111–4.113. [[validation/HYBRID_DOMAIN]].

§4.71: native runtime reference_height_m;8 supplied-height captures, target/road tradeoff. Таблица4.72, рисунки4.114–4.116. [[validation/REFERENCE_HEIGHT]].

§4.72: native runtime local_support_patch;8 supplied-geometry captures и joint target/ground evaluation. Таблица4.73, рисунки4.117–4.119. [[validation/LOCAL_SUPPORT]].

§4.73: native runtime source_occlusion_box;8 supplied-visibility captures, уменьшение ложных копий и обнаружение неподдержанного target. Таблица4.74, рисунки4.120–4.122. [[validation/SOURCE_VISIBILITY]].

§4.74: center/texel/source-RGB атрибуция всех497 visibility residual pixels,290 controls; primary frozen audit и отдельный post-hoc geometry drill-down. Таблица4.75, рисунки4.123–4.125. [[validation/VISIBILITY_RESIDUAL]].

Native radial footprint visibility (bilinear_radial_v1, guard0/1) реализована через configure_surface/catalog и проверена на16 frozen captures (§4.75). Outside local198/56→30/6→1/0, но visible hits76/69→12/10→2/2: preservation gate не пройден, default center сохранён. Oracle0 mismatches/2591739 pairs, full build/CTest7/7, carrier10/10 Mesa/NVIDIA, independent20/20, reaudit identical. Далее surface-consistent footprint/preservation и paired tolerance/geometry uncertainty; general mitigation не закрыта. [[validation/FILTER_VISIBILITY]].

§4.76: native bilinear_box_surface_v1 сохраняет central-visible finite-box support,24 matched captures/16 historical hashes exact. Local guard1 outside198/56→1/0, visible75/76 и69/69, IoU0.418994/0.401163; один boundary pixel frame0 не проходит10µm membership (расстояние0.159502mm), strict preservation gate остаётся открытым. Default center сохранён. Oracle0/4319565, CTest7/7, carrier10/10 Mesa/NVIDIA, independent21/21, reaudit identical. Далее paired tolerance/geometry uncertainty; known box не general surface/depth estimator. [[validation/BOX_SURFACE_VISIBILITY]].

§4.77: native support_tolerance_m через configure_surface/catalog;76 frozen sensitivity outputs,8 historical hashes exact, oracle0/15985665, CTest7/7, carrier10/10 Mesa/NVIDIA, independent22/22, reaudit identical. Nominal1mm band/20mm bias сохраняет76/76 и69/69, outside1/0, IoU0.424581/0.401163; при±5mm Z geometry errors robustness не пройдена, широкая полоса может увеличивать false acceptance. Gates23/54 outputs,7/27 paired cells. Default center сохранён; далее uncertainty/unsupported policy и независимый holdout. [[validation/VISIBILITY_SENSITIVITY]].

§4.78: post-hoc exhaustive audit74 local outputs,54 surface outputs имеют600 central/303 remaining-footprint loss memberships на145 unique frame/pixels;18 center controls200 central/0 footprint. Hidden target102/103: off marker hits102/103 не доказывают восстановления скрытых поверхностей. Nominal1mm hits76/69 при IoU0.424581/0.401163 близки к условному visible-only ceiling0.426966/0.401163 (не universal bound). Independent23/23, CTest1/1, reaudit identical; native code не менялся. Проект unsupported/support diagnostics ещё требует native реализации: [[architecture/UNSUPPORTED_SURFACES]], [[validation/VISIBILITY_FAILURE]].

§4.81: bounded central uncertainty audit на18 прежних center controls/шести радиусах,108 rows; достаточные guaranteed/optimistic eligibility bounds и reference-risk. При5mm все200 loss memberships потенциально снимаются, но1125/1305 visible memberships ambiguous; не новый RGB render или готовая mitigation. Таблица4.83, рисунки4.141–4.143. [[validation/VISIBILITY_UNCERTAINTY]].

§4.82: native accept/reject uncertainty policies,54 matched NVIDIA RGBA captures,18 historical controls exact; restoration/reference-blocking/footprint tradeoff, robust joint gates не пройдены. Таблица4.84, рисунки4.144–4.146. [[validation/NATIVE_UNCERTAINTY]].

§4.83: joint uncertainty/finite-face-facing native кандидат;36 enabled и4 disabled NVIDIA captures,oracle0/8407961, CTest57/57. Z+5mm accept/surface10mm проходит пять gates, но nominal/Z−5mm оставляют reference failures; robust fixed профиль не найден. Таблица4.85, рисунки4.147–4.149. [[validation/NATIVE_FACING]].

§4.84: remaining entry-gap audit36 outputs/30 memberships; quantified grazing amplification и negative fixed central2/0mm filters, без новой product реализации/captures. Таблица4.86, рисунки4.150–4.152. [[validation/FACING_ENTRY_GAP]].

§4.85: native source_visibility_v1 и typed SV01 delivery; accepted uncertainty и причины отказа без изменения legacy UV/status. CTest58/58, NVIDIA25/25. Таблица4.87, рисунки4.153–4.154. [[validation/SOURCE_VISIBILITY_PRODUCT]].

§4.86: all9 residual coordinates/all4 cameras, independent scene hits и triangle oracle69/69; same-object/wrong-point и отсутствие matching alternatives. Таблица4.88, рисунки4.155–4.158. [[validation/RESIDUAL_SURFACE]].

§4.87: runtime evidence contract и optional native fallback display, catalog v5, typed API и GUI button, полный Mesa CTest59/59. Таблица4.89, рисунки4.159–4.160. [[validation/FALLBACK_DISPLAY]].

§4.88: native encoded RGB disagreement и typed wire API; frozen photometric evidence screen с2 failed historical pairings и old/current Mesa controls. Таблица4.90, рисунки4.161–4.163. Полный CTest59/59 не подменяет scientific pairing gate. [[validation/SOURCE_DISAGREEMENT]].

§4.89: Decimal80 finite-face oracle,32 precision replays и axis substitutions объясняют central decision flip от endpoint drift; strict predicate/defaults сохранены. Таблица4.91, рисунки4.164–4.166. [[validation/ENDPOINT_PRECISION]].

§4.90: matched current-Mesa UV/source truth,36 native captures и3520-cell lattice inventory; same-pair RGB1/20 counterexample centre-confidence threshold. Таблица4.92, рисунки4.167–4.169. [[validation/MESA_SOURCE_TRUTH]].

§4.91: две новые неоднородные Blender scenes, native plane/patch baseline и separate RGB ROI evaluation; target overlap отсутствует и остаётся следующим этапом. Таблицы4.93–4.94, рисунки4.170–4.172. [[validation/TEXTURED_SOURCE]].
