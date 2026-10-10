# Парное сравнение pyramid boundaries на production сервере

11.10.2026. Frozen protocol: [[research/SERVER_BOUNDARY_PROTOCOL]]. Native C++ renderer/runner, независимый read-only анализ `tools/research/server_boundary.py`. Raw metadata и результаты: [статическая серия](baselines/server_boundary_v1.json), [moving-target серия](baselines/server_boundary_motion_v1.json). Это exploratory опыты на прежних Blender данных, не holdout.

## Исполнение и контроль

| Серия | Matrix | Samples с warmup | Captured measurements | Независимые сцены |
|---|---|---:|---:|---:|
| Статическая | 2 позы × 5 носителей × 2 fusion × 2 boundaries | 360 | 280 | Одна прежняя улица |
| Moving target | 9 кадров × dome × 2 fusion × 2 boundaries | 324 | 252 | Одна прежняя траектория |

На условие два warmup и семь measurement blocks, случайный полный порядок, seed 20261011. Native reports проверены по исходным config/manifest/capture hashes, exact scenario timestamps, четырём входам, calibration IDs, settings/revisions, opaque RGBA SHA-256, полноте matrix и повторяемости output. Финальный hash совпал с baseline **последнего** кадра; все шесть запусков восстановили fusion/surface/pause. Cursor/history не возвращаются. Полные native reports включены в baselines; тяжёлые RGBA и журналы остались в `artifacts/server-boundary-v2` и `artifacts/server-boundary-motion-v1`.

Использован Linux/GLES Mesa **AMD Ryzen 9 9950X integrated radeonsi**, не RTX и не Аврора. Catalog хранит renderer/vendor/source_revision/source_fingerprint. Метка revision `d4036cb` — HEAD перед рабочими изменениями; идентификатор фактического дерева renderer/runner — fingerprint `6f96ae936fcf5ea59903e3874677e32c4694b1f1f040a2dccafdcc4c2c22443a`. Испытания выполнялись последовательно без CTest в фоне. Timing включает renderer wall spans, не end-to-end sensor/display latency.

## Результаты

В таблице среднее **парных разностей normalized−zero** по кадрам/носителям; повторные рендеры не считаются независимыми наблюдениями. Для timing сначала берётся медиана семи samples внутри условия, затем средняя парная разность. Statistical significance и универсальное ранжирование не заявляются.

| Серия / режим | Δ linear RGB MAE | Δ target IoU | Δ median fusion CPU, ms |
|---|---:|---:|---:|
| Static / multi_band | −0.00021651 | −0.00190991 | +0.35888 |
| Static / graph_cut_multi_band | −0.00002781 | +0.00031153 | +0.46665 |
| Motion / multi_band | −0.00013861 | −0.00160427 | +0.11010 |
| Motion / graph_cut_multi_band | +0.00000925 | −0.00074422 | +1.63112 |

Static RGB MAE уменьшилась в 20/20 пар, target IoU улучшилась в одной, ухудшилась в шести и не изменилась в 13. На девятикадровой серии RGB улучшилась в 14/18 пар и ухудшилась в четырёх; IoU ухудшилась в восьми и не изменилась в десяти. Следовательно, сохранение constant field не гарантирует улучшения геометрии объекта или RGB на каждом текстурированном кадре. Default zero сохранён; normalized остаётся кандидатом для дальнейшей ablation.

RGB ROI задан независимыми ray-cast visibility/object interiors до анализа outputs. Coded-target IoU измеряет фиксированный chroma classifier против direct object ID; touching duplicate проверяется known-answer control, но natural-object ghost detector этим не заменён. Raw результаты также содержат precision/recall, false-positive/missing pixels и p95 channel error. Изображения и сравнительный график: [[diploma/04_EXPERIMENTAL_STUDY#4.45 Серверная проверка normalized support на текстурированной сцене]].

## Что остаётся

Сетки разных носителей здесь не имеют равного triangle/memory budget. Короткая paused sequence не проверяет temporal history, optical-flow correspondence, непрерывную photometry, sustained FPS или thermal. Сохранённая motion серия имеет timestamps 0…800 ms без wrap, но её nine frames не становятся девятью независимыми сценами. Новый Blender capture недоступен: MCP не соединяется, локальный Blender binary не найден. Нужны новые независимые сцены/монтажные seeds, длинные клипы и равные budgets; multilabel seam и другие calibration families этим опытом не реализованы.

Воспроизведение native сценариев и независимого audit: [[engineering/USAGE#Многокадровый native эксперимент и RGBA capture]]. Скрипт графиков расположен рядом с дипломными главами: `docs/diploma/plot_server_boundary.py`.
