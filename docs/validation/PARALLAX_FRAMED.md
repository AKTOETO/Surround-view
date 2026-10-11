# Полный объект в кадре и два монтажа: native исследование

11.10.2026. Протокол [[research/PARALLAX_FRAMED_PROTOCOL]], [baseline](baselines/parallax_framed_v1.json), диплом §4.55. Новые paired Blender captures при общем поддерживаемом server view, два mount seeds101/104. Это продолжение контролируемого опыта, не независимый scene-family holdout.

## Стенд и проверки

Flat road100×100m, ego, emission box0.4m; near/far XY и center heights0.2/0.8/1.4m прежние. View azimuth0.8/elevation0.6rad, distance10m/fov_y1.6rad, origin target, clip0.1…100m;320×180 output,400px fisheye из256px cubefaces. Два vehicle poses с перемещением0.4m. Фактические perturbed extrinsics подаются серверу, calibration solver здесь не запускается.

Минимальный запас projected полного box до borders10.179px, frozen ceiling≥8px проверен на всех24 case/pose сочетаниях. Ray-cast target pixels дополнительно должны лежать внутри analytic projected bounds; contact с краем кадра отвергается. Это не только проверка видимого ID-фрагмента. Known-answer tests подтверждают exact pinhole box bounds, near/far rejection, out-of-frame detection и guards конфаундеров.

Внутри каждого rig совпадают view/config/trajectory/timestamps. Между rigs отличаются только camera extrinsics, intrinsics/non-camera config/poses совпадают. Independent direct RGB обоих poses для каждого из шести факторов совпадает между seeds побитно после decode; visibility и source RGB не обязаны совпадать. Audit сохраняет hashes шести paired direct controls и реальные mount offsets. Один scene family/материал/размер остаётся ограничением.

| Camera | Seed101 yaw/pitch, ° | Seed101 along body, m | Seed104 yaw/pitch, ° | Seed104 along body, m |
|---|---:|---:|---:|---:|
| 0 front | 2.661 / −0.843 | 0.0570 | 2.031 / 1.153 | −0.0568 |
| 1 right | 0.548 / −1.234 | 0.0845 | −2.248 / −0.750 | 0.0508 |
| 2 rear | 2.216 / −0.815 | 0.0946 | 1.411 / −0.523 | 0.0290 |
| 3 left | −1.653 / 1.833 | 0.0362 | 1.945 / −2.602 | 0.0077 |

Это две реализации bounds ±3°/±0.1m, не sweep optical height/baseline. Optical heights остаются0.85/1.12m. Native source fingerprint `601a3878ba99e6bc953756bbc0c40e969fdb0c89c5d7487349332b7960ac87f4`; base binary revision2240202 со сборкой inspection code, позже сохранённого в27f1cc2. Product C++ в этом этапе не изменялся.

## Прозрачность исправления постановки

Первая exploratory recipe задавала Blender optional target_m=(1.5,1.5,0.4). Production config parser отклонил её на первом geometry probe до measurement samples. Unsupported recipe/captures сохранены в `artifacts/parallax-framed-inputs-v1/master-plan.json`, runtime master —`artifacts/parallax-framed-v1/failed-master-plan.json`. Результаты этой попытки не включены в baseline.

До финального capture выбран view вокруг origin и выполнена проверка всех пяти конфигураций compiled sv-carrier-probe: `artifacts/parallax-framed-config-check-v2`. Final recipe/protocol зафиксированы до финальных data/quality результатов. Framed validator теперь отвергает неподдерживаемые server view fields заранее. Optional Blender target_m не означает поддержку такого поля продуктовым API.

## Воспроизведение

Final captures/replay: `artifacts/parallax-framed-inputs-v2`, native reports/RGBA/logs: `artifacts/parallax-framed-v2`. Создаются60 fresh servers,12 cases×5 medium carriers×4 profiles×2 poses =480 quality conditions. Warmup2/repeats7:4320 samples/3360 measurement RGBA captures; geometry probes отдельно. CPU-fusion/NVIDIA GLES paused fixed-order trials не являются sustained FPS или target performance.

Из корня проекта после нового Blender capture:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-framed-plan.json --root artifacts/parallax-framed-repeat --inputs artifacts/parallax-framed-repeat-inputs --run-binaries build --output artifacts/parallax-framed-repeat/audit.json
```

Повторная проверка существующей серии и рисунки:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-framed-plan.json --root artifacts/parallax-framed-v2 --inputs artifacts/parallax-framed-inputs-v2 --output artifacts/parallax-framed-v2/reaudit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report docs/validation/baselines/parallax_framed_v1.json --captures-root artifacts/parallax-framed-v2 --inputs-root artifacts/parallax-framed-inputs-v2
```

Мир восстанавливается прежним checked-in generator без .blend/LFS: [[engineering/BLENDER#Полный объект в кадре: два варианта монтажа]]. Новых художественных downloads нет: [[engineering/ASSETS#A05 — Полностью помещающийся в кадр объект, два монтажа]]. Python выполняет исследовательский export/audit; продуктовая сшивка —в C++ server, управление —sv-client-lib/native svctl.

## Результаты всех480 условий

Диапазоны по5 carriers×4 profiles×2 poses. Conditional range включает только определённые значения; отдельный count=null не скрывается.

| Rig / фактор | Full-mask IoU min…max | Conditional IoU min…max | Undefined / 40 |
|---|---:|---:|---:|
| 101 near-z020 | 0.052632…0.110553 | 0.007634…0.017241 | 0 |
| 101 near-z080 | 0 | 0 | 0 |
| 101 near-z140 | 0 | 0 | 0 |
| 101 far-z020 | 0.171171…0.338816 | 0.128788…0.271739 | 0 |
| 101 far-z080 | 0 | 0 | 0 |
| 101 far-z140 | 0 | 0 | 20 |
| 104 near-z020 | 0.056122…0.110000 | 0.008174…0.017143 | 0 |
| 104 near-z080 | 0 | 0 | 0 |
| 104 near-z140 | 0 | 0 | 0 |
| 104 far-z020 | 0.170251…0.343234 | 0.128060…0.276364 | 0 |
| 104 far-z080 | 0 | 0 | 0 |
| 104 far-z140 | 0 | 0 | 20 |

Полный box не обрезается ни в одном case/pose; ID border contact0/480. Full-mask IoU0 во320/480 условиях, все —поднятые center heights0.8/1.4m. Conditional IoU0 в280 из440 определённых условий;40 undefined относятся к far-z140 второго pose обоих rigs, где direct target131 pixels, но any-camera visible target0. Первому far-z140 pose соответствуют151 direct pixels и лишь3 visible pixels. Near-z140 имеет48/49 direct pixels и1/4 visible. Ненулевой raw source chroma присутствует хотя бы в одной камере всех24 case/pose сочетаний. Следовательно, исчезновение coded output не объясняется отсутствием всех исходных target observations или обрезанием самого box, но не все direct грани наблюдаемы и не все tiny fragments надёжны для обобщения.

Near-z080 имеет coded predictions414–1682 pixels (rig101) и308–1504 (rig104), но overlap с truth0: объект смещён/растянут. Near-z140/far-z080/far-z140 имеют predicted pixels0 после component policy. Full IoU и counts различают эти причины; RGB MAE этого не делает. Все inputs/reports/measurement RGBA, бюджеты, restore/cursors и повторяемость проверены.

Normalized boundary снизил aggregate RGB MAE во240/240 zero/normalized пар. Full IoU ухудшилась10/240, совпала230/240; улучшений нет. Conditional IoU ухудшилась10/240, совпала210/240, неопределена20/240. Это не независимые повторения: пары относятся к одному стенду/двум rigs. Улучшение цветов однородной дороги не является исправлением силуэта.

Пары rig104 minus rig101 сопоставлены по carrier/truth_index/mode/boundary, а не по порядку строк отчёта. Для near-z020 Δfull IoU=−0.000553…+0.005462 (14 лучше/18 хуже/8 ties), far-z020=−0.002611…+0.007455 (26/10/4). У320 raised-object условий оба rigs имеют full IoU0. Два монтажа подтверждают повторяемость негативного результата в этих условиях, но не дают статистический рейтинг mount robustness или calibration algorithms.

## Проверки и ограничения

CTest object_metrics/blender_fixture/render_fusion_parity/render_modes:4/4,5.88s. Повторный полный audit480 условий совпал с baseline побитно. Старый conditional report240 условий после расширения analyzer совпал по всем полям, кроме нового analyzer file digest. Blender вернулся к исходной Scene; legacy helpers не менялись.

Новый view меняет размер/набор видимых поверхностей; разность с прежним clipped опытом нельзя приписывать только clipping. Source visibility остаётся геометрическим point oracle, не natural correspondence. Не проверены другие optical heights/широкие baselines, новые scene families/материалы/разрешения, длинные клипы и target Aurora. На heatmap среднее conditional score двух poses undefined, если хотя бы один pose undefined; такой pose не выбрасывается через nanmean. Финальное исследовательское заключение не закрыто.

Hardware erratum: frozen protocol ошибочно описывал CPU-Mesa на основании environment flag. Фактические catalog всех60 native reports: backend=opengl_es3, gl_vendor=NVIDIA Corporation, gl_renderer=NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2. LIBGL_ALWAYS_SOFTWARE=1 не переключил GLVND на Mesa. В новом baseline render_device берётся из самих catalogs и проверяется на согласованность; fusion CPU и GLES rendering различаются. Та же проверка всех30 прежних height и15 lateral reports дала NVIDIA. Historical JSON/plain protocol сохранены как запись первоначальной формулировки, их Mesa label не является достоверным паспортом стенда. Числовые измерения не менялись; это исправление provenance interpretation, не повтор timed trials.
