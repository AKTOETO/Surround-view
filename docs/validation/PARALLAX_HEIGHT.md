# Управляемая высота объекта: серверный опыт

11.10.2026. Протокол [[research/PARALLAX_HEIGHT_PROTOCOL]], [полный baseline](baselines/parallax_height_v1.json), глава4 §4.53. Один flat-ground world/perturbed rig seed101, шесть факторов, два зависимых poses; это initial controlled screen, не holdout.

## Выполнено

Blender5.2.2LTS: шесть isolated scenes, box0.4×0.4×0.4m, center z0.2/0.8/1.4m, XY near=(2.4,2.3), far=(4.8,4.6)m. Дорога100×100m, ego и camera locators сохранены; прочие meshes исключены. Root/local provenance фиксируют master/helper hashes и фактический mesh inventory. Legacy static camera capture не записывает evaluated target position; она проверяется в direct truth. Исходная активная Scene восстановлена.

30 fresh production sv-server процессов, пять medium carriers, четыре multi_band/graph_cut_multi_band × zero/normalized profiles, два кадра, warmup2/repeats7:2160 samples/1680 measurement captures/240 quality conditions. Native geometry probes идут отдельно. Config/calibration, poses/timestamps и virtual view одинаковы между факторами. Все RGBA/manifest/report hashes, budget constraints, restore/cursors и source fingerprint проверены независимым audit. Product C++ не менялся.

Fingerprint `601a3878ba99e6bc953756bbc0c40e969fdb0c89c5d7487349332b7960ac87f4`; binary metadata base revision2240202 со сборкой inspection изменений27f1cc2, как в [[validation/CARRIER_LATERAL]]. CPU/Mesa, paused trials, fixed order; timing не является sustained FPS или target performance.

## Результат всех факторов

Диапазоны по пяти carriers, четырём profiles и двум poses; target component policy min8/chroma0.15. Размер truth —full-frame ID mask, не только source-visible часть.

| Фактор | Truth pixels | Any-camera visible truth pixels | Native predicted pixels | IoU min…max | Нулевой IoU |
|---|---:|---:|---:|---:|---:|
| near-z020 | 69–79 | 20–26 | 191–619 | 0.03715…0.08787 | 0/40 |
| near-z080 | 71–82 | 15–19 | 373–2578 | 0 | 40/40 |
| near-z140 | 79–85 | 2–5 | 0 | 0 | 40/40 |
| far-z020 | 403–417 | 237–327 | 290–768 | 0.45441…0.72059 | 0/40 |
| far-z080 | 477–627 | 241–350 | 0 | 0 | 40/40 |
| far-z140 | 549–744 | 24–28 | 0 | 0 | 40/40 |

В каждом case target chroma присутствует хотя бы в одной входной камере обоих poses; disappearance output нельзя объяснить отсутствием всех source observations. Однако лишь часть direct target surface видима source cameras, особенно near/верхний уровень. Не следует объявлять весь full-mask deficit исключительно ошибкой fusion. При near-z080 colored output присутствует, но смещён и не пересекается с truth; при near-z140/far-z080/far-z140 после component policy coded output отсутствует. Нулевой IoU не различает эти причины без pixel counts.

Гипотетические ground intersections лучей через центр box: при z0.2 displacement0.437–2.603m; при z0.8 —5.028–135.353m; при z1.4 все16 лучей имеют t<0. Optical centers z0.85/1.12m. Это контроль ground approximation, не расчет видимой поверхности box и не доказательство поведения dome/cube shell. Ни один исследованный fusion/profile/carrier не восстановил поднятый target в этой серии.

Far-z020 обрезан нижней границей direct frame; размер/visibility меняются при переносе. Поэтому нельзя заключать «увеличение расстояния улучшает алгоритм». Homogeneous road доминирует RGB MAE, она не заменяет object IoU. Два poses и один rig не позволяют статистически обобщать результат. Natural correspondence, source-visible object IoU, иные baselines/mounts/views/resolutions и depth-aware geometry остаются работой.

## Воспроизведение и проверки

Captures: `artifacts/parallax-height-inputs-v1`, native reports: `artifacts/parallax-height-v1`; они generated и не включены в Git. Checked-in recipes/driver восстанавливают стенд без .blend/LFS. Команды: [[engineering/BLENDER#Управляемая высота и расстояние объекта]], [[engineering/USAGE#Управляемая высота и параллакс]]. Рисунки сохраняет versioned `docs/diploma/plot_server_boundary.py` рядом с главой.

Known-answer controls проверяют ground intersections, parallel/behind/null, camera baseline disparity и centroid cancellation; factor validator отвергает changed range/size/view/height. Targeted CTest render_modes/blender_fixture/object_metrics/render_fusion_parity:4/4,13.75s. После фиксации analyzer повторный read-only audit должен совпасть побитно с baseline. Первоначальный автоматический audit остановился на отсутствующем legacy static position поле; исправлена проверка реально экспортируемого direct truth, native данные не пересчитывались и не выбирались заново.
