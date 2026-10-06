# Проверка аналитического CPU-эталона

Дата: 06.10.2026. Реализация: `3ce330e`, тесты: `7ecbfed`. Инструменты: `tools/reference.py`, `tools/compare_reference.py`; команды/форматы — [[engineering/RENDERING#Независимый аналитический CPU-эталон]].

Серия `artifacts/analytic-reference-final`: пять носителей × два ракурса × три fusion = 30 случаев. Использованы только кадр 0 и четыре исходных изображения записи `artifacts/blender-street`, 960×540, manifest SHA-256 `d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc`. Config/hash всех входов/implementation сохранены ниже в первичных JSON. Полная геометрия вычислена на CPU аналитически, без треугольной сетки. Blender world не изменялся.

## Методика и результат

Виртуальная камера: azimuth 0.8 rad, distance 8.5 m; oblique elevation 1.0 rad, low 0.35 rad. Размеры носителей совпадают с screening; параметры сетки для аналитического вычисления не используются. ROI — hit за вычетом footprint с margin; он зависит от носителя. Наблюдаемость здесь означает только допустимость fisheye-проекции в изображение. Истина заслонения не вычисляется.

У трёх замкнутых носителей carrier misses равны нулю в обоих ракурсах. У плоскости/bowl низкий ракурс имеет соответственно 173711/149474 misses из 518400 выходных пикселей; oblique — 10386/1534. Закрытая оболочка устраняет геометрический выход за край, однако не исправляет разнесённые optical centers, параллакс и двоение.

Равные агрегированные доли coverage у купола/цилиндра/куба не означают одинаковых проекций: пространственные границы overlap и отображение объектов различаются, что видно на рисунке. Никакой ranking носителей или fusion по качеству здесь не получен. RGB-ошибка CPU/GPU ещё не вычислялась; для неё нужна маска видимого кузова и согласованные silhouettes/background.

## Проверки реализации

Десять unittest случаев: устойчивые quadratic/linear roots; dome floor/shell и clip depth; 4096 направлений из внутренней точки каждой замкнутой оболочки без misses; cylinder/cube caps/tangent/miss; bowl patches и plane; 10000 независимых направлений с проверкой bowl residual; top-left и центральный луч; axis/FOV/behind-camera и билинейный RGB; linear-RGB mean/hard tie/valid border с нулевым весом; хэши и запрет overwrite отчёта.

Сборка Release выполнена; полный CTest 13/13 групп прошёл за 36.01 s. После добавления all-directions проверки группа `analytic_reference` повторно прошла, включая все десять случаев. Аппаратные камеры/Аврора/две физические машины не испытывались. Native suite сохраняет прежние 25 критериев/10 нагрузок; host-reference не добавлен в RPM. Baselines/RPM 0.6.0 остаются историческими результатами своих ревизий; CMake регистрация нового теста входит в fingerprint новой сборки.

## Иллюстрация

![[diploma/figures/experiments/04_analytic_reference.png]]

*Рисунок V.1 — Аналитический CPU render пяти носителей и карты допустимых проекций при низком ракурсе. Серое исключено из ROI; чёрное слева у plane/bowl — лучи без carrier hit. Автомобильный overlay отсутствует. Источник: сохранённые результаты 30-case серии, скрипт `docs/diploma/plot_reference.py`.*

## Первичный отчёт серии

Ниже сохранён полный машинный отчёт в текстовом Markdown; большие point/weight maps и отдельные previews остаются в игнорируемой папке artifacts.

### Dense CPU reference: пять носителей

Один кадр, два общих виртуальных ракурса; без треугольной дискретизации.

| Носитель | View | Fusion | Miss pixels | ROI pixels | Valid projection % | Positive weight % |
|---|---|---|---:|---:|---:|---:|
| dome | oblique | edge_feather | 0 | 490344 | 99.85235 | 99.85235 |
| dome | oblique | hard_best_angle | 0 | 490344 | 99.85235 | 99.85235 |
| dome | oblique | angular_feather | 0 | 490344 | 99.85235 | 99.85235 |
| dome | low | edge_feather | 0 | 506302 | 99.94154 | 99.94154 |
| dome | low | hard_best_angle | 0 | 506302 | 99.94154 | 99.94154 |
| dome | low | angular_feather | 0 | 506302 | 99.94154 | 99.94154 |
| cylinder | oblique | edge_feather | 0 | 490344 | 99.85235 | 99.85235 |
| cylinder | oblique | hard_best_angle | 0 | 490344 | 99.85235 | 99.85235 |
| cylinder | oblique | angular_feather | 0 | 490344 | 99.85235 | 99.85235 |
| cylinder | low | edge_feather | 0 | 506302 | 99.94154 | 99.94154 |
| cylinder | low | hard_best_angle | 0 | 506302 | 99.94154 | 99.94154 |
| cylinder | low | angular_feather | 0 | 506302 | 99.94154 | 99.94154 |
| cube | oblique | edge_feather | 0 | 490344 | 99.85235 | 99.85235 |
| cube | oblique | hard_best_angle | 0 | 490344 | 99.85235 | 99.85235 |
| cube | oblique | angular_feather | 0 | 490344 | 99.85235 | 99.85235 |
| cube | low | edge_feather | 0 | 506302 | 99.94154 | 99.94154 |
| cube | low | hard_best_angle | 0 | 506302 | 99.94154 | 99.94154 |
| cube | low | angular_feather | 0 | 506302 | 99.94154 | 99.94154 |
| plane | oblique | edge_feather | 10386 | 479958 | 99.84915 | 99.84915 |
| plane | oblique | hard_best_angle | 10386 | 479958 | 99.84915 | 99.84915 |
| plane | oblique | angular_feather | 10386 | 479958 | 99.84915 | 99.84915 |
| plane | low | edge_feather | 173711 | 332591 | 99.91100 | 99.91100 |
| plane | low | hard_best_angle | 173711 | 332591 | 99.91100 | 99.91100 |
| plane | low | angular_feather | 173711 | 332591 | 99.91100 | 99.91100 |
| bowl | oblique | edge_feather | 1534 | 488810 | 99.85189 | 99.85189 |
| bowl | oblique | hard_best_angle | 1534 | 488810 | 99.85189 | 99.85189 |
| bowl | oblique | angular_feather | 1534 | 488810 | 99.85189 | 99.85189 |
| bowl | low | edge_feather | 149474 | 356828 | 99.91705 | 99.91705 |
| bowl | low | hard_best_angle | 149474 | 356828 | 99.91705 | 99.91705 |
| bowl | low | angular_feather | 149474 | 356828 | 99.91705 | 99.91705 |

ROI различается между носителями. Доля покрытия не ранжирует качество.

### Первичные отчёты

```json
{
  "schema_version": 1,
  "scope": "analytic carrier validity; no physical scene visibility or quality ranking",
  "rows": [
    {
      "carrier": "dome",
      "view": "oblique",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "ed6ece73e0916a458c7b1e066715d0db7144160db6e7232e4a38da3e27dd80d8",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "dome_floor_v1",
          "dome_radius_m": 12.0,
          "dome_latitude_cells": 64,
          "dome_longitude_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.396208744001342,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277702,
          211918,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "dome",
      "view": "oblique",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "88e2081c44a304b140de8d85e4226545a901ee0439176f5f7600c2d4001531b5",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "dome_floor_v1",
          "dome_radius_m": 12.0,
          "dome_latitude_cells": 64,
          "dome_longitude_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.7402407439949457,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277702,
          211918,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "dome",
      "view": "oblique",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "7e83a3c56d796b06721b1bbae49d2f83eed9adc52ddb05d5eacf3d64f65dc87e",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "dome_floor_v1",
          "dome_radius_m": 12.0,
          "dome_latitude_cells": 64,
          "dome_longitude_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.41045869098161347,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277702,
          211918,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "dome",
      "view": "low",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "9caa6619923f619eb50dfb9a86110a590009bffb6d1041129a54f16eb96de6c4",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "dome_floor_v1",
          "dome_radius_m": 12.0,
          "dome_latitude_cells": 64,
          "dome_longitude_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.40464865099056624,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          265385,
          240621,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "dome",
      "view": "low",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "e3695115948bf2f25e9ceb66e8ff7b29d501b137c11fdadd82827e54f8c4ebca",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "dome_floor_v1",
          "dome_radius_m": 12.0,
          "dome_latitude_cells": 64,
          "dome_longitude_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.38374448200920597,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          265385,
          240621,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "dome",
      "view": "low",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "816a86fd70f4fe74487771cf79053d68215a1b4cf638c6609413d3a9780abcf7",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "dome_floor_v1",
          "dome_radius_m": 12.0,
          "dome_latitude_cells": 64,
          "dome_longitude_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.3629459840012714,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          265385,
          240621,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cylinder",
      "view": "oblique",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "cd459a8eb6d3d0ccc6c0b91ca6a97c6fcad94e4f70d0a97d30ea32487c4cb551",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cylinder_floor_v1",
          "radius_m": 12.0,
          "height_m": 12.0,
          "vertical_cells": 32,
          "angular_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.38538294401951134,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277706,
          211914,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cylinder",
      "view": "oblique",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "5d64405a09a0e765f48dfac6da11f4f431c7cebb3e26c1e25e2f21ed032c65a7",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cylinder_floor_v1",
          "radius_m": 12.0,
          "height_m": 12.0,
          "vertical_cells": 32,
          "angular_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.39684707499691285,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277706,
          211914,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cylinder",
      "view": "oblique",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "110b3272277340ed354f058e25362c0ad7a73f1ba180564660219b873c2d2d60",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cylinder_floor_v1",
          "radius_m": 12.0,
          "height_m": 12.0,
          "vertical_cells": 32,
          "angular_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.6146341490093619,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277706,
          211914,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cylinder",
      "view": "low",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "bf28e570a5f1a2f4b1bddfcced4de2a351913a0f207494c83f423ab0a959e444",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cylinder_floor_v1",
          "radius_m": 12.0,
          "height_m": 12.0,
          "vertical_cells": 32,
          "angular_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.395137062005233,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          265036,
          240970,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cylinder",
      "view": "low",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "cec3eddd9ccd46c40cc0db1e6529e632b950cb78f905fb410cb664f86a263cdf",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cylinder_floor_v1",
          "radius_m": 12.0,
          "height_m": 12.0,
          "vertical_cells": 32,
          "angular_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.4194639030029066,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          265036,
          240970,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cylinder",
      "view": "low",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "bcc01bb843974b0b7f6877decce7851c3b7aee1ffd7575da3018836cd7fe6d33",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cylinder_floor_v1",
          "radius_m": 12.0,
          "height_m": 12.0,
          "vertical_cells": 32,
          "angular_cells": 128,
          "floor_radial_cells": 32
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.39551788099925034,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          265036,
          240970,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cube",
      "view": "oblique",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "118da11c863634878cdba16e695402b7c00a0adeac75194432262812cee4f3d8",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cube_floor_v1",
          "half_extent_m": 12.0,
          "height_m": 12.0,
          "face_cells": 32
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.43035829201107845,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277726,
          211894,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cube",
      "view": "oblique",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "820cb379d8e946956b980a034a5839b937d8b89d47e251119360e6b5d5e4be86",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cube_floor_v1",
          "half_extent_m": 12.0,
          "height_m": 12.0,
          "face_cells": 32
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.41115007700864226,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277726,
          211894,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cube",
      "view": "oblique",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "a98a9fbb9ddb09cb6c5b6f4d6a0471195458987ddca9cdef0ab5fc3687fc15a2",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cube_floor_v1",
          "half_extent_m": 12.0,
          "height_m": 12.0,
          "face_cells": 32
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.4220925070112571,
        "evaluation_pixels": 490344,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          724,
          277726,
          211894,
          0,
          0
        ],
        "observed_fraction": 0.9985234855529995,
        "positive_weight_fraction": 0.9985234855529995,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cube",
      "view": "low",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "3b51231dc8e16bef6aa41e1470dd4cdbae0ca2bf597d4f77c9888182a98b47df",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cube_floor_v1",
          "half_extent_m": 12.0,
          "height_m": 12.0,
          "face_cells": 32
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.41231630399124697,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          264961,
          241045,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cube",
      "view": "low",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "ea30fe18935b40f5db09ed2936c95966a98761e05866e7998d8635e6628d99f2",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cube_floor_v1",
          "half_extent_m": 12.0,
          "height_m": 12.0,
          "face_cells": 32
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.3967635959852487,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          264961,
          241045,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "cube",
      "view": "low",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "72b853e6fb78c2cd04e9fa044b0b15eb13af2e245187c3302ff1c9ef7a528480",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "cube_floor_v1",
          "half_extent_m": 12.0,
          "height_m": 12.0,
          "face_cells": 32
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.40493620798224583,
        "evaluation_pixels": 506302,
        "carrier_miss_pixels": 0,
        "coverage_histogram": [
          296,
          264961,
          241045,
          0,
          0
        ],
        "observed_fraction": 0.9994153686929935,
        "positive_weight_fraction": 0.9994153686929935,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "plane",
      "view": "oblique",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "a6214b2a6acde41e87b926e0e6160606823cead7ce20fdee22d20de20d40477f",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 0.0,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.49241429098765366,
        "evaluation_pixels": 479958,
        "carrier_miss_pixels": 10386,
        "coverage_histogram": [
          724,
          271276,
          207958,
          0,
          0
        ],
        "observed_fraction": 0.9984915346759509,
        "positive_weight_fraction": 0.9984915346759509,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "plane",
      "view": "oblique",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "c5bc30b39421716d827683e0177b2ed338c136c1be2f34783c7f8fc22b88720f",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 0.0,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.5127161949931178,
        "evaluation_pixels": 479958,
        "carrier_miss_pixels": 10386,
        "coverage_histogram": [
          724,
          271276,
          207958,
          0,
          0
        ],
        "observed_fraction": 0.9984915346759509,
        "positive_weight_fraction": 0.9984915346759509,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "plane",
      "view": "oblique",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "7213c2163e722ffa05a5403f04c6570c17fe94adb22e9e0f913cdfb86e85d02d",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 0.0,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.5180647659872193,
        "evaluation_pixels": 479958,
        "carrier_miss_pixels": 10386,
        "coverage_histogram": [
          724,
          271276,
          207958,
          0,
          0
        ],
        "observed_fraction": 0.9984915346759509,
        "positive_weight_fraction": 0.9984915346759509,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "plane",
      "view": "low",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "9d7abd800c18585fa38a7415f24c8265cd719239337fcff839d0c7455ca08496",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 0.0,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.5051740610215347,
        "evaluation_pixels": 332591,
        "carrier_miss_pixels": 173711,
        "coverage_histogram": [
          296,
          180669,
          151626,
          0,
          0
        ],
        "observed_fraction": 0.9991100180101085,
        "positive_weight_fraction": 0.9991100180101085,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "plane",
      "view": "low",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "c521e089d0947e1581193f897f20abc9d4151cb158ca1f7b2e236e8b07f245fe",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 0.0,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.500891997013241,
        "evaluation_pixels": 332591,
        "carrier_miss_pixels": 173711,
        "coverage_histogram": [
          296,
          180669,
          151626,
          0,
          0
        ],
        "observed_fraction": 0.9991100180101085,
        "positive_weight_fraction": 0.9991100180101085,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "plane",
      "view": "low",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "62fb10ef32bc1e11ee53c5a126a82966a9c4759d602c9f7f16904a00d623186d",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 0.0,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.4864880740060471,
        "evaluation_pixels": 332591,
        "carrier_miss_pixels": 173711,
        "coverage_histogram": [
          296,
          180669,
          151626,
          0,
          0
        ],
        "observed_fraction": 0.9991100180101085,
        "positive_weight_fraction": 0.9991100180101085,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "bowl",
      "view": "oblique",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "8c0f6b60e2f9e1c56c2c41b63feee2f767821fddb34d5df335cdd230f1b601f0",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 1.5,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.4896879869920667,
        "evaluation_pixels": 488810,
        "carrier_miss_pixels": 1534,
        "coverage_histogram": [
          724,
          280807,
          207279,
          0,
          0
        ],
        "observed_fraction": 0.9985188519056484,
        "positive_weight_fraction": 0.9985188519056484,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "bowl",
      "view": "oblique",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "16532100080d666d9ed5d3bb36335a763e54e0eeba366c6aeff344ad6df43ad2",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 1.5,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.7246369530039374,
        "evaluation_pixels": 488810,
        "carrier_miss_pixels": 1534,
        "coverage_histogram": [
          724,
          280807,
          207279,
          0,
          0
        ],
        "observed_fraction": 0.9985188519056484,
        "positive_weight_fraction": 0.9985188519056484,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "bowl",
      "view": "oblique",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "08a7077f6d26d4e39321dd3800e3928bcff380e32b842f81f72c401c18ff961a",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 1.5,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.48619830099050887,
        "evaluation_pixels": 488810,
        "carrier_miss_pixels": 1534,
        "coverage_histogram": [
          724,
          280807,
          207279,
          0,
          0
        ],
        "observed_fraction": 0.9985188519056484,
        "positive_weight_fraction": 0.9985188519056484,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "bowl",
      "view": "low",
      "mode": "edge_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "4f6cd9bc2699842ea9dd4a869d3bd009dbd745c70d881152d3142034edd5c626",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 1.5,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "edge_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.49693017097888514,
        "evaluation_pixels": 356828,
        "carrier_miss_pixels": 149474,
        "coverage_histogram": [
          296,
          188493,
          168039,
          0,
          0
        ],
        "observed_fraction": 0.9991704686851929,
        "positive_weight_fraction": 0.9991704686851929,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "bowl",
      "view": "low",
      "mode": "hard_best_angle",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "b5960184e3d519ca0a82f1ba68fcf0198918092e16078bc3e421aedddb660865",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 1.5,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "hard_best_angle",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.5105490740097594,
        "evaluation_pixels": 356828,
        "carrier_miss_pixels": 149474,
        "coverage_histogram": [
          296,
          188493,
          168039,
          0,
          0
        ],
        "observed_fraction": 0.9991704686851929,
        "positive_weight_fraction": 0.9991704686851929,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    },
    {
      "carrier": "bowl",
      "view": "low",
      "mode": "angular_feather",
      "report": {
        "schema_version": 1,
        "suite_id": "surround-view-analytic-reference-v1",
        "config_sha256": "0bfa30afcdccf81b1ee384a95fd7bb3a1aa4589ebbc89c76c4eef1e0ff0e393a",
        "manifest_sha256": "d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc",
        "implementation_sha256": "fc7ba7b13b9d9daea3075ae65db57bfc48751e3d2ccd4bd6dc3ed4eb5a968488",
        "input_sha256": {
          "camera0_0000.ppm": "d8de34841a6b89242c7958a643981c61a721303ef78ea9030d38088bee3b72ee",
          "camera1_0000.ppm": "9f20a4287cbb3b371a8bad23c12ff45744d9c153955aa5605ff6ec043fc18ee0",
          "camera2_0000.ppm": "b3f084ab0bdba742a8c3d42f928a41e3c8e68a902cda2491356a0d0c26628a29",
          "camera3_0000.ppm": "cfd1e10c184f0eb6ccac6c8a372c81216908d0b115183ac69f74fa049531468b"
        },
        "frame_index": 0,
        "scenario_timestamp_ns": "0",
        "surface": {
          "type": "rectangular_bowl_v1",
          "flat_half_length_m": 2.6,
          "flat_half_width_m": 1.2,
          "outer_half_length_m": 12.0,
          "outer_half_width_m": 12.0,
          "corner_height_m": 1.5,
          "uniform_cells": [
            64,
            64
          ]
        },
        "fusion": {
          "mode": "angular_feather",
          "edge_width_px": 24.0,
          "angle_power": 2.0
        },
        "output": {
          "width": 960,
          "height": 540
        },
        "render_seconds": 0.5080117739853449,
        "evaluation_pixels": 356828,
        "carrier_miss_pixels": 149474,
        "coverage_histogram": [
          296,
          188493,
          168039,
          0,
          0
        ],
        "observed_fraction": 0.9991704686851929,
        "positive_weight_fraction": 0.9991704686851929,
        "limitations": [
          "analytic carrier, not physical scene depth",
          "no vehicle overlay or occlusion",
          "footprint mask alone is not sufficient for GPU image comparison",
          "single offline frame; no seam/ghosting/temporal quality conclusion"
        ]
      }
    }
  ]
}
```
