# Сравнение платформ

Базовая платформа: **PC-RTX5070Ti-v06**. Целевая: **PC-Mesa-v06**.

Сопоставимость нагрузки: **подтверждена**.

Отношение времени относится к медиане p95 повторов render/readback. Оно не является отношением FPS всей системы или физической задержки.

| Вариант | База p95, мс | Цель p95, мс | Цель / база |
|---|---:|---:|---:|
| bowl | 0.18550 | 0.88564 | 4.774 |
| bowl_720p | 1.01499 | 2.55288 | 2.515 |
| bowl_dense | 0.18873 | 1.54764 | 8.200 |
| bowl_upload | 0.31353 | 1.39143 | 4.438 |
| cube_floor | 0.16995 | 0.94180 | 5.541 |
| cylinder_floor | 0.16729 | 1.75588 | 10.496 |
| dome_angular | 0.17389 | 1.60234 | 9.214 |
| dome_floor | 0.19787 | 1.68357 | 8.508 |
| dome_hard | 0.17768 | 1.48836 | 8.377 |
| plane | 0.17683 | 0.82534 | 4.667 |

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
      "baseline_p95_ms": 0.18549604999999997,
      "target_p95_ms": 0.8856414499999999,
      "target_to_baseline_ratio": 4.774449105520037
    },
    {
      "variant": "bowl_720p",
      "baseline_p95_ms": 1.01498885,
      "target_p95_ms": 2.55288425,
      "target_to_baseline_ratio": 2.5151845264113
    },
    {
      "variant": "bowl_dense",
      "baseline_p95_ms": 0.18872714999999998,
      "target_p95_ms": 1.5476416499999999,
      "target_to_baseline_ratio": 8.200418699694241
    },
    {
      "variant": "bowl_upload",
      "baseline_p95_ms": 0.3135273,
      "target_p95_ms": 1.3914296000000002,
      "target_to_baseline_ratio": 4.437985464104721
    },
    {
      "variant": "cube_floor",
      "baseline_p95_ms": 0.16995469999999993,
      "target_p95_ms": 0.9417966999999999,
      "target_to_baseline_ratio": 5.54145722360135
    },
    {
      "variant": "cylinder_floor",
      "baseline_p95_ms": 0.16729155,
      "target_p95_ms": 1.75588175,
      "target_to_baseline_ratio": 10.495938079359059
    },
    {
      "variant": "dome_angular",
      "baseline_p95_ms": 0.17389359999999993,
      "target_p95_ms": 1.6023425,
      "target_to_baseline_ratio": 9.21449955605037
    },
    {
      "variant": "dome_floor",
      "baseline_p95_ms": 0.1978719,
      "target_p95_ms": 1.68357215,
      "target_to_baseline_ratio": 8.5083943197594
    },
    {
      "variant": "dome_hard",
      "baseline_p95_ms": 0.17767795,
      "target_p95_ms": 1.48835605,
      "target_to_baseline_ratio": 8.37670656375763
    },
    {
      "variant": "plane",
      "baseline_p95_ms": 0.17682884999999998,
      "target_p95_ms": 0.82533965,
      "target_to_baseline_ratio": 4.667449061620884
    }
  ]
}
```
