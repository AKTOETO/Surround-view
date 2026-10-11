# Чувствительность к выходному разрешению, 11.10.2026

Парный опыт480 условий при320×180→640×360 сохранил320 нулевых raised overlaps. High conditional scores теперь определены во всех480 случаях;40 прежних undefined стали определёнными0. Component thresholds8/32 дали одинаковые high metrics. Полный paired audit воспроизводится побитно.

## Уточнение до native quality results, 11.10.2026

Frozen protocol [[research/PARALLAX_OUTPUT_PROTOCOL]]/commit8d6bcdc требует прежних source observations. После нового Blender capture, **до запуска native quality matrix**, первые cube PNG имеют побитно одинаковые decoded RGB, но разные file SHA256: Blender PNG text metadata Date/Camera/Scene/RenderTime меняется между captures. Capture metadata/config после исключения output dimensions совпадает; иных изменений постановки не обнаружено.

Приёмка уточнена явно: файлы каждой серии по-прежнему проверяются по собственным manifest SHA256. Между сериями проверяются decoded RGB **всех реально экспортированных cube faces**, dimensions/mode и PNG metadata после исключения только Date/Camera/Scene/RenderTime. Fisheye RGB должны совпасть побитно. Разные RGB или иные metadata блокируют сравнение. Frozen protocol не переписан; отличие от требования равенства raw-file hashes обозначено как deviation. Численные/алгоритмические условия и критерии вывода не меняются.

Одновременно выявлена ошибка bookkeeping предыдущего sampling audit: `checked_cube_faces=96` было константой, хотя exporter сохраняет только нужные fisheye rays faces, а не обязательно6 граней каждой камеры. Реальные файлы в прежнем audit проверялись циклом, но опубликованный counter и derived prose были завышены. Counter исправлен на число фактически открытых/проверенных faces; предыдущий paired baseline будет перепроверен и исправлен только по analyzer digest/counter. Numerical results, native hashes и controls не меняются. Это provenance correction, не повтор trials.

Полный source preflight до native matrix принят:12 cases,960 реальных cube faces обеих серий. Decoded RGB и статические PNG metadata совпали. В прежнем sampling report исправлено960 фактически проверенных faces (80 на case pair); до/после correction совпали все поля, кроме analyzer digest и counter. Source faces в этой постановке по5 на камеру/pose; `overview.png` входит в capture manifest, но не считается source face.

Дополнительное identity bookkeeping уточнение при проверке truth setup: Blender сохраняет уникальные суффиксы имён `.036`/`.048` и т.п. в разных сериях; numeric object IDs и соответствующие semantic base names совпадают. Audit сравнивает ID→base-name таблицу после удаления только terminal numeric suffix, сохраняет original tables обеих серий в report и строго проверяет target/helper/ego IDs, source-visibility definition/tolerance и все прочие поля. Это не разрешение произвольного переименования, перестановки ID или изменения геометрии. Source pixels уже приняты полным preflight; native quality results до этого уточнения не анализировались. Literal name-table equality из протокола уточнена и внесена в report deviations.

## Завершённая серия и provenance

Native root `artifacts/parallax-output-v1`, inputs `artifacts/parallax-output-inputs-v1`; reference root/inputs/full summary SHA256 закреплены в `configs/research/parallax-output-plan.json`. Baseline `baselines/parallax_output_v1.json` содержит480 keyed pairs,12 controls с decoded cube/fisheye digests, original truth ID tables и RGB/ID/visibility hashes/counts обеих серий,60+60 native report hashes и обе RGBA hashes каждой пары. Алгоритмы C++ не изменены; Python выполняет audit/independent metrics, не fusion.

60 fresh native processes,4320 samples,3360 measurement RGBA,480 conditions.960 фактических source cube faces проверены;480 raw PNG pairs отличаются hashes при одинаковом decoded content/static metadata. Source camera inputs400×400 совпали побитно. Full-box margin high минимум20.358621px, ID frame-border contact0. Product source fingerprint `601a3878ba99e6bc953756bbc0c40e969fdb0c89c5d7487349332b7960ac87f4` прежний. CPU fusion; фактический backend=opengl_es3, vendor=NVIDIA Corporation, renderer=NVIDIA GeForce RTX5070Ti/PCIe/SSE2 в обеих сериях.

## Результаты всех480 пар

| Метрика | Low320×180 |High640×360,8px |High640×360,32px |
|---|---:|---:|---:|
| Raised full IoU0 |320/320 |320/320 |320/320 |
| Undefined conditional |40 |0 |0 |
| Defined conditional0 |280 |320 |320 |
| Undefined centroid |240 |240 |240 |

High8/high32 metrics совпадают **во всех480 строках**, не только по aggregate counts. Ground full IoU93 улучшений/67 ухудшений (−0.012317…+0.013367), raised320 ties0. Conditional deltas96 positive/64 negative/280 zero/40 undefined; последние40 —low undefined→high0, не численное улучшение. Normalized centroid155 уменьшаются/85 увеличиваются/240 undefined; near-z080 расстояние0.284821…0.331941 высоты кадра, против low0.284299…0.331383. Predicted-area fraction166 уменьшаются/39 увеличиваются/275 ties; направление не является универсальным quality criterion. Linear RGB MAE478 увеличений/2 уменьшения, но discrete ROI и pixel-unit support различаются между resolutions: это не чистая оценка fusion ухудшения.

| Far-z140 pose |Low target / visible pixels |High target / visible pixels, оба rigs |
|---|---:|---:|
|0 |151 /3 |616 /34 |
|1 |131 /0 |548 /32 |

Все320 raised high conditional scores определены и равны0. Новые direct rays обнаруживают tiny visible fragments там, где low raster их пропускал. Это sensitivity sampled visibility, не доказательство непрерывной occlusion в прежнем кадре; visibility не равна source-pixel correspondence. Near-z140 high first pose202/11; second190/7 (rig101) или190/8 (rig104). Фрагменты малы и не дают natural-object/широкий mount вывод.

## Воспроизведение и границы вывода

Повторный полный read-only paired audit совпал с baseline побитно. CTest object_metrics/blender_fixture/render_fusion_parity/render_modes:4/4,6.21s. Дополнительно object_metrics36 tests проверяют output/budget/source confounders, normalized units/null и relative area threshold, сохранение semantic IDs/tolerance. Старый fixture не имел modern diagnostic_motion_script_sha256: unit setup дополнен известным placeholder digest; actual study поле остаётся обязательным. Blender восстановил исходную Scene.

Pyramid levels/edge settings/ROI boundary suppression остаются в pixels: физический support меняется. Вывод ограничен full-pipeline output-resolution sensitivity при прежних sources/geometry/algorithms. Output320×180 сам по себе не объясняет провал raised overlap; sensor/fisheye sampling, source-surface correspondence/triangle-hit и геометрическая модель ещё не разделены полностью.64/256/512 source-convergence из других scenes нельзя механически объединять с этой серией как независимые реплики. Нет universal carrier/fusion ranking, timing benchmark, natural-scene holdout или target Aurora acceptance.

Команды/figures: [[engineering/USAGE#Чувствительность к выходному разрешению]], [[engineering/BLENDER#Независимый truth640×360]]. Диплом §4.57/рисунки4.75–4.77. Raw outputs generated; source recipes, baseline и captioned figures tracked. Frozen protocol8d6bcdc сохранён; приёмочные deviations и коррекция прежнего face counter зафиксированы cab57b9/489e302 до quality analysis.
