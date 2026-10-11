# Парный cubemap sampling control, 11.10.2026

Протокол до capture: [[research/PARALLAX_SAMPLING_PROTOCOL]], commit5942925. Анализ/guards: `tools/research/parallax_height.py`; продуктовые C++ algorithms не изменены. Baseline: `baselines/parallax_sampling_v1.json`; диплом §4.56/рисунки4.72–4.74.

## Входы и приёмка

Низкое разрешение: `artifacts/parallax-framed-inputs-v2` / `artifacts/parallax-framed-v2`, tracked `baselines/parallax_framed_v1.json`. Высокое: `artifacts/parallax-sampling-inputs-v1` / `artifacts/parallax-sampling-v1`. Recipe `assets/scenarios/parallax-sampling-v1.json` отличается от framed только `capture.face_size`:256→512. Runtime matrix совпадает за исключением experiment/input-plan/protocol identifiers. Fisheye400×400/output320×180, два mounts101/104,12 height/range cases,5 carriers,4 profiles,2 poses,2 warmups/7 repeats.

Новые60 fresh servers создали4320 samples/3360 measurement RGBA/480 quality conditions. Capture и trials выполнялись последовательно; Blender вернулся в исходную Scene. Все960 кубических грани двух серий проверены по hashes/размерам. На всех12 case pairs конфигурация, stamps/poses, capture metadata (кроме разрешения/hashes), binaries и decoded direct RGB/object-ID/raw visibility camera bits совпадают. Все480 native RGBA hashes между resolutions различаются. Высокодетальный chroma присутствует хотя бы в одной source camera всех24 case/pose сочетаний.

Прежний полный baseline воспроизводится по всем numerical/input/native/protocol fields; допускаются только digests расширенного analyzer и metric module, как в existing historical check. Новый paired baseline содержит12 control digests, raw native-report hashes обеих серий и480 keyed rows с обоими RGBA hashes, исходными метриками и signed deltas. Key `(case,carrier,truth_index,mode,boundary)` не зависит от порядка записей. Native implementation fingerprint `601a3878ba99e6bc953756bbc0c40e969fdb0c89c5d7487349332b7960ac87f4` сохраняется. Device обеих серий: CPU fusion + `opengl_es3`, NVIDIA Corporation / NVIDIA GeForce RTX5070Ti/PCIe/SSE2. Environment software flag не считается паспортом Mesa.

## Результаты

| Метрика, Δ=512−256 | Отрицательная | Нулевая | Положительная | Undefined | Δ min…max |
|---|---:|---:|---:|---:|---:|
| Full IoU | 101 | 320 | 59 | 0 | −0.025509…+0.011355 |
| Conditional IoU | 93 | 280 | 67 | 40 | −0.025441…+0.009939 |
| Predicted area | 238 | 242 | 0 | 0 | −33…0 pixels |
| Centroid distance | 52 | 0 | 188 | 240 | −0.298048…+0.540049px |
| Linear RGB MAE | 228 | 0 | 252 | 0 | −0.0001564…+0.00006187 |

Направление улучшения зависит от метрики: положительная IoU лучше; отрицательная distance/MAE лучше. Area не имеет универсального направления улучшения. Undefined сохраняются, не приравниваются нулю. Все320 raised full IoU остаются0; ground160 имеют ненулевые deltas смешанного знака. Conditional defined set440 сохраняется:280 zero и160 positive.40 undefined —far-z140 второго pose обоих mounts, независимо от source texture resolution.

Near-z080 при512px: rig101 predicted404–1665 pixels/centroid52.660–58.980px, rig104 predicted305–1489/centroid51.174–59.649px, но full overlap0. Остальные raised cases по-прежнему без surviving predicted component. Увеличение детализации cubemap не устраняет observed displacement/disappearance в этой постановке; RGB MAE и object shape отвечают на разные вопросы.

## Проверка и ограничения

CTest object_metrics/blender_fixture/render_fusion_parity/render_modes:4/4,5.82s. Known-answer tests отвергают изменение view/triangle budgets, неверные face sizes, неполные/дублирующиеся key sets; перестановка строк не меняет pairing. Проверена defined→undefined transition без ложного zero delta. Повторный полный read-only paired audit совпадает с tracked baseline побитно. Legacy Blender/converter helpers не менялись.

Это контроль только cubemap256→512: fixed fisheye/output resolutions и filtering/component thresholds сохраняются. Он отвергает недостаточную cubemap256 детализацию как достаточное объяснение320 нулевых raised overlaps, но не измеряет отдельно все геометрические/sampling contributions. Два mounts и два poses одной flat scene не являются480 независимыми репликами. Нельзя выбирать universal carrier/fusion, оценивать natural correspondence/calibration solver или переносить timing на Аврору. Следуют отдельные sensor/output resolution, triangle-hit/correspondence, optical-height/baseline и независимые scene-family tests.

Команды capture/run/pair/figures: [[engineering/BLENDER#Парный контроль разрешения cubemap]], [[engineering/USAGE#Парный контроль разрешения cubemap]]. Raw outputs остаются generated artifacts; source recipes, audit baseline и подписанные figures versioned.

Erratum11.10.2026 перед output-resolution follow-up: exporter пишет5 нужных граней на camera/pose,40 на case/series. Реальное число проверенных faces обеих серий —960, а не1152. В коде проверки файлов уже шли по фактическим faces; ошибочным был только опубликованный hardcoded counter96 на case pair. Он заменён вычисляемым80; повторный paired audit подтвердил неизменность всех иных полей, кроме analyzer digest. Numerical results/native hashes сохранены.
