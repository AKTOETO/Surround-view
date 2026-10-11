# Видимая поверхность объекта: повторная оценка native outputs

11.10.2026. Протокол [[research/PARALLAX_VISIBILITY_PROTOCOL]], [baseline](baselines/parallax_visibility_v1.json), диплом §4.54. Это повторная оценка240 outputs из [[validation/PARALLAX_HEIGHT]], без новых native запусков и без новых независимых данных.

## Проверка происхождения

Прежний full-mask baseline сохранён без изменения. Повторный audit воспроизводит все его результаты, native/input/config/report hashes, poses, budgets, restore и protocol digests. Исключены только digests расширенных analyzer и object_metrics.py; historical/current digests явно записаны. Все числовые результаты сравниваются точно. Тесты отвергают изменение прежней IoU и native RGBA hash. Source fingerprint остаётся `601a3878ba99e6bc953756bbc0c40e969fdb0c89c5d7487349332b7960ac87f4`.

Новая conditional IoU исключает unobserved truth pixels из denominator и не штрафует predictions внутри них, но сохраняет false positives вне полного target. Visibility берётся из independent paired truth. Component filtering выполняется до visibility restriction; фрагменты2/5 pixels верхнего близкого target остаются в оценке. Пустой visible target даёт null, а не успешную оценку.

## Все факторы

Диапазоны по пяти carriers, четырём profiles и двум poses. Frame-border count —число условий с контактом full target mask с краем кадра, не число независимых наблюдений.

| Фактор | Visible target pixels | Conditional IoU min…max | Conditional recall min…max | Border contact |
|---|---:|---:|---:|---:|
| near-z020 | 20–26 | 0…0.003906 | 0…0.038462 | 0/40 |
| near-z080 | 15–19 | 0 | 0 | 0/40 |
| near-z140 | 2–5 | 0 | 0 | 0/40 |
| far-z020 | 237–327 | 0.276151…0.668675 | 0.544304…1 | 20/40 |
| far-z080 | 241–350 | 0 | 0 | 0/40 |
| far-z140 | 24–28 | 0 | 0 | 0/40 |

Conditional IoU ниже full-mask IoU в80 условиях, совпадает в160, выше —ни в одном. Это не свойство формулы для всех данных: denominator и intersection меняются одновременно. В near-z020 значительная доля совпадений прежнего full-mask screen относится к unobserved truth surface, поэтому прежние0.037–0.088 нельзя трактовать как восстановление наблюдаемой поверхности. При raised target160/160 conditions остаются нулевыми даже на visible support. В каждом кадре visible target непуст; undefined scores в этой серии отсутствуют.

Ненулевой source chroma и ray visibility не гарантируют correspondence, достаточного sampling или recoverability цвета. Новая метрика отделяет часть дефицита наблюдений, но не локализует ошибку до fusion/geometry/calibration. Visibility truth не является ручной разметкой natural objects. Contact flag срабатывает для far-z020 первого pose (20 profiles/carriers); второй pose не касается края. Это не разрешает causal near/far ranking. Нужен следующий capture с fully framed targets, другими mounts/baselines и независимыми scene families.

## Воспроизведение

Используются прежние `artifacts/parallax-height-inputs-v1` и `artifacts/parallax-height-v1`. Из корня проекта:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-height-plan.json --root artifacts/parallax-height-v1 --inputs artifacts/parallax-height-inputs-v1 --visibility-baseline docs/validation/baselines/parallax_height_v1.json --output artifacts/parallax-height-v1/visibility-audit.json
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_server_boundary.py --report artifacts/parallax-height-v1/visibility-audit.json --captures-root artifacts/parallax-height-v1 --inputs-root artifacts/parallax-height-inputs-v1
```

`--visibility-baseline` несовместим с `--run-binaries`: это audit исторических измерений. Повторный output должен совпасть побитно с checked-in visibility baseline при тех же версиях кода и данных. Historical full-mask baseline остаётся отдельным документом. Рисунки показывают все шесть factors/первый pose/fixed dome-MB-zero и полную матрицу средних двух poses. Tests: known-answer support/FP/ignored/null/filter/border cases и historical tamper rejection; результаты запуска ниже отражены в журнале главы.

Проверено: CTest object_metrics/blender_fixture/render_fusion_parity —3/3,5.78s; два запуска visibility audit совпали побитно. Product C++ и тайминговые данные не изменялись.
