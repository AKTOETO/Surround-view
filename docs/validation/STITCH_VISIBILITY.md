# E-STITCH-01: depth-based carrier visibility screening

Date: 09.10.2026. Scope: one static Blender scene/frame, five analytic carriers, two output views, three fusion modes. This is a reproducible **geometric visibility screening**, not a seam-quality or ghosting benchmark. Full question, literature review and confirmation protocol: [[research/PROJECTION_AND_STITCHING]].

## Reproduction and provenance

The tool reads the same four RGB frames, calibration, and radial-depth maps. It verifies RGB/depth checksums, camera IDs, synchronous timestamps, and dimensions. For each rendered carrier point `X` and camera `i`, it projects `X`, reads measured radial depth `d_i`, and compares it with camera-to-point range `r_i=||T_i X||`. A sample is depth-consistent when `|d_i-r_i|≤max(0.05 m,0.01 r_i)`. It records projection coverage separately from exact-depth coverage and apportions normalized fusion weight to matching depth, a foreground occluder, a first hit behind the carrier point, or no depth return.

```sh
python3 tools/compare_visibility.py \
  --dataset artifacts/blender-depth-truth-dataset-fixed \
  --output artifacts/stitch-visibility-v3

python3 tools/compare_visibility.py \
  --dataset artifacts/blender-depth-truth-dataset-fixed \
  --output artifacts/stitch-visibility-tol-002 \
  --carriers dome plane --views low --modes edge_feather hard_best_angle \
  --depth-tolerance-m 0.02

python3 tools/compare_visibility.py \
  --dataset artifacts/blender-depth-truth-dataset-fixed \
  --output artifacts/stitch-visibility-tol-010 \
  --carriers dome plane --views low --modes edge_feather hard_best_angle \
  --depth-tolerance-m 0.10
```

The first-frame capture metadata SHA-256 is `a41c598b0ae9a3fce217c2c81eb8d794d67014c10c0b3889fe8c2cf35bed94a5`; config SHA-256 `cdf90d578d165f63734678a0709e9bc3a71199e13ba7095771bd351e4d1a9bca`; RGB manifest SHA-256 `5109162ec2a52b0de176b64bb27a2469f0a77c3eb4ba84f3191114c4ef7ac7a8`. The dataset uses face_size=32; image, depth and script hashes are also saved in the generated JSON report. Repeatable capture/conversion commands are in [[engineering/BLENDER]] and depth validation is in [[validation/DEPTH_VISIBILITY_TRUTH]]. Generated large artifacts are intentionally kept under ignored `artifacts/`, while these compact measurements and the diagnostic illustration are checked in.

Implementation hashes for the recorded run: `compare_visibility.py` `9c79b1708212a0a23972dd98a042c951124fe013213d71774c141b347eb081cc`; `reference.py` `fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488`; `depth_truth.py` `68881656ff26fd5720c90b28a7d99fc352bc689c704a8599922c833bd418db38`. The checked-in diagnostic image is PNG SHA-256 `3999a343cbcc6671ef4c3924393e93ed172e7e52684b3a7cead0582b67eb8615`.

## Low-view measurements

| Carrier | Projection coverage | Exact-depth point coverage | Matching weight: edge | angular | hard | Edge weight on foreground occluder | Edge weight with no surface at carrier point |
|---|---:|---:|---:|---:|---:|---:|---:|
| Plane | 99.91% | 24.31% | 19.81% | 20.77% | 21.17% | 40.40% | 39.79% |
| Bowl | 99.92% | 21.96% | 17.89% | 18.64% | 18.93% | 14.93% | 67.18% |
| Dome + floor | 99.94% | 16.20% | 13.18% | 13.79% | 14.05% | 43.02% | 34.90% |
| Cylinder + floor/cap | 99.94% | 16.27% | 13.26% | 13.87% | 14.14% | 43.31% | 34.78% |
| Cube shell | 99.94% | 16.30% | 13.28% | 13.91% | 14.19% | 44.85% | 33.35% |

### Чувствительность depth tolerance

Поскольку depth capture имеет только 32×32 texels на cube face, дополнительно проверены абсолютные tolerance 0.02/0.05/0.10 м при одинаковом относительном члене 1%. Сравнены low-view plane/dome и edge/hard; coverage ниже — точная видимость любой камеры, а matching weight относится к fusion режиму.

| Tolerance, м | Carrier | Exact-depth coverage | Matching weight: edge | Matching weight: hard |
|---:|---|---:|---:|---:|
| 0.02 | Dome + floor | 11.12% | 8.79% | 9.28% |
| 0.05 | Dome + floor | 16.20% | 13.18% | 14.05% |
| 0.10 | Dome + floor | 27.12% | 22.26% | 23.94% |
| 0.02 | Plane | 16.59% | 13.12% | 13.90% |
| 0.05 | Plane | 24.31% | 19.81% | 21.17% |
| 0.10 | Plane | 40.94% | 33.64% | 36.23% |

При этих трёх thresholds plane остаётся выше dome по exact-depth coverage в данном low-view, а hard имеет немного большую depth-consistent weight долю, чем edge. Однако абсолютные значения заметно меняются при изменении tolerance: это диагностический экран, не достаточно точная база для выбора carrier/fusion. Confirmation требует face-size convergence на конкретной scene geometry и заранее заданного threshold, проверенного на более высоком разрешении.

The exact-depth coverage is independent of the fusion mode for a fixed carrier: it asks whether any camera observes the queried 3D point. The three weight columns show how each direct fusion strategy distributes contribution. Oblique view exact-depth coverage ranges from 23.82% (bowl) to 25.34% (plane); the other three closed carriers are 24.80%. All are one frame from one synthetic world.

![Доли fusion-веса по совпадению с depth truth](assets/stitch_visibility_dome_low.png)

*Рисунок V.3 — Нормированный fusion weight для dome low-view edge-feather: красный — первая поверхность ближе точки носителя; зелёный — глубина совпала; синий — точка носителя находится перед первым depth hit; белый — depth truth не имеет возврата. Смесь каналов показывает смесь источников. Чёрная область — ROI, исключённая по маске автомобиля.*

## Интерпретация

Почти 100% проекционной coverage не означает, что texture sample показывает поверхность, находящуюся в данной точке носителя. На куполе low-view совпадение с точкой составляет 16.20%; при edge-feather только 13.18% суммарного веса приходится на depth-consistent samples, а 43.02% — на sample, перекрытый более близкой геометрией. Это подтверждает необходимость отдельной проверки visibility и объясняет, почему цветовое покрытие само по себе недостаточно для выбора mesh.

В этом одном кадре hard-best-angle поднимает совпадающий вес относительно edge-feather примерно на 0.9 процентного пункта у купола и на 1.4 п.п. у плоскости, но hard не показывает выигрыш по coverage и создаёт резкую границу выбора. Это недостаточно, чтобы предпочесть hard: измерялась глубинная согласованность выборок, не визуальный seam, duplicate contour, резкость, temporal flicker или реальный camera object-ID.

Bowl имеет в этой сцене меньшую долю ближнего occluder weight, но сам факт не означает лучшую картинку: большая доля carrier points находится перед первым известным hit, а carrier ROI у разных форм различается. Доля «точка перед первым hit» не равна пустому пикселю итогового кадра; она означает лишь, что в источнике нет depth-подтверждения физической поверхности именно в `X`.

## Границы и следующий опыт

Глубина получена из Blender EEVEE на 32×32 cube faces; аналитическая плоскость показывает, что ошибка дискретизации зависит от face size/дальности/угла. Сцена не даёт длинного клипа, независимых material/object-ID masks или физического автомобиля. Аналитический CPU renderer вычисляет fusion; native server/GPU readback здесь не прогонялся. ROI исключает только footprint автомобиля и не является полной visibility mask кузова.

Из отчёта нельзя заключить, что dome, plane, bowl, hard или feather лучше в реальной машине. Для такого вывода нужны отдельные S0–S6 validation scenes, ID/semantic render pass, holdout pose seeds и видео. Graph-cut, seam-distance feather, exposure compensation и multi-band пока не реализованы и не измерены. Точные метрики/политика/бюджеты последующего confirmation campaign зафиксированы в [[research/PROJECTION_AND_STITCHING#10 Зафиксированный экспериментальный контракт]].
