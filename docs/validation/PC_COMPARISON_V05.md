# Сравнение платформ

Базовая платформа: **PC-RTX5070Ti-v05**. Целевая: **PC-Mesa-v05**.

Сопоставимость нагрузки: **подтверждена**.

Отношение времени относится к медиане p95 повторов render/readback. Оно не является отношением FPS всей системы или физической задержки.

| Вариант | База p95, мс | Цель p95, мс | Цель / база |
|---|---:|---:|---:|
| bowl | 0.16982 | 0.89906 | 5.294 |
| bowl_720p | 0.98448 | 2.57899 | 2.620 |
| bowl_dense | 0.16307 | 1.53959 | 9.441 |
| bowl_upload | 0.28369 | 1.33185 | 4.695 |
| cube_floor | 0.15893 | 0.93366 | 5.875 |
| cylinder_floor | 0.16625 | 1.60605 | 9.660 |
| dome_angular | 0.15545 | 1.72185 | 11.076 |
| dome_floor | 0.19516 | 1.60945 | 8.247 |
| dome_hard | 0.15432 | 1.44775 | 9.382 |
| plane | 0.16510 | 1.36788 | 8.285 |

| Критерий | База | Цель |
|---|---|---|
| CACHED_INPUT_UPLOAD | pass | pass |
| CONFIG_STRICT | pass | pass |
| EGL_GLES_FBO | pass | pass |
| ENCLOSURE_MESH | pass | pass |
| FIXTURE_HASHES | pass | pass |
| GPU_ENCLOSURE_COVERAGE | pass | pass |
| GPU_FUSION_MODES | pass | pass |
| GPU_OPENCV_PROJECTION | pass | pass |
| GPU_RGBA_TOP_LEFT | pass | pass |
| HASH_SHA256 | pass | pass |
| IMAGE_RGB_ORIGIN | pass | pass |
| MATH_CENTRAL_INVALID | pass | pass |
| MATH_OPENCV | pass | pass |
| MESH_WINDING | pass | pass |
| OPENCV_BOARD | pass | pass |
| OPENCV_INTRINSICS | pass | pass |
| OUTPUT_DIMENSIONS | pass | pass |
| PROTOCOL_FRAGMENTED | pass | pass |
| PROTOCOL_LIMITS | pass | pass |
| RENDER_VARIANTS | pass | pass |
| SRGB_LINEAR | pass | pass |
| SYNC_COMPLETE_STALE | pass | pass |
| SYNC_ORDER_BOUND | pass | pass |
| SYNC_SKEW | pass | pass |
| TRANSFORM_INVERSE | pass | pass |

Версии OpenCV и компилятора, архитектура CPU и GL_RENDERER остаются частью паспортов платформ; они могут различаться при переносе.

```json
{
  "schema_version": 1,
  "comparable": true,
  "reasons": [],
  "render": [
    {
      "variant": "bowl",
      "baseline_p95_ms": 0.1698202,
      "target_p95_ms": 0.8990553499999999,
      "target_to_baseline_ratio": 5.294160235354804
    },
    {
      "variant": "bowl_720p",
      "baseline_p95_ms": 0.9844801999999999,
      "target_p95_ms": 2.5789890499999992,
      "target_to_baseline_ratio": 2.6196454230364403
    },
    {
      "variant": "bowl_dense",
      "baseline_p95_ms": 0.1630708,
      "target_p95_ms": 1.5395883,
      "target_to_baseline_ratio": 9.441226142264586
    },
    {
      "variant": "bowl_upload",
      "baseline_p95_ms": 0.28368705,
      "target_p95_ms": 1.33184605,
      "target_to_baseline_ratio": 4.694772108913678
    },
    {
      "variant": "cube_floor",
      "baseline_p95_ms": 0.1589273,
      "target_p95_ms": 0.93365565,
      "target_to_baseline_ratio": 5.874734233828927
    },
    {
      "variant": "cylinder_floor",
      "baseline_p95_ms": 0.1662527,
      "target_p95_ms": 1.6060515,
      "target_to_baseline_ratio": 9.660303261240268
    },
    {
      "variant": "dome_angular",
      "baseline_p95_ms": 0.15545405,
      "target_p95_ms": 1.7218463,
      "target_to_baseline_ratio": 11.076239570471145
    },
    {
      "variant": "dome_floor",
      "baseline_p95_ms": 0.1951621,
      "target_p95_ms": 1.6094496,
      "target_to_baseline_ratio": 8.24673233173859
    },
    {
      "variant": "dome_hard",
      "baseline_p95_ms": 0.15431825,
      "target_p95_ms": 1.44775195,
      "target_to_baseline_ratio": 9.381599065567423
    },
    {
      "variant": "plane",
      "baseline_p95_ms": 0.16510305,
      "target_p95_ms": 1.3678830999999998,
      "target_to_baseline_ratio": 8.285026230587501
    }
  ]
}
```
