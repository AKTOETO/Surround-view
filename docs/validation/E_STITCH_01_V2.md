# E-STITCH-01 v2: пересчитанная exploratory matrix

Дата прогона: 2026-10-09T20:13:20Z. Статус: **матрица пересчитана после аудита, но исследование качества не закрыто**. Это одна синтетическая статическая Blender-сцена × 6 carriers × 2 virtual views × 7 fusion modes (84 случая), не независимая подтверждающая серия.

## Воспроизводимость и вычислительная среда

Входы хранятся в `tests/data/e_stitch_01_v1`: четыре RGB PPM 400×400, четыре radial-depth NPY float32 400×400, manifest, ground truth и calibration config. Runner проверяет SHA-256 файлов, соответствие порядка camera IDs и calibration IDs, разрешения RGB/depth и синхронность кадров. Исходная сцена и происхождение описаны в [[engineering/ASSETS]] и `assets/scenes/metric-street/provenance.json`.

```sh
python3 tools/run_e_stitch_01.py
```

Команда использует tracked fixture и пишет previews/JSON/markdown в игнорируемую `artifacts/e-stitch-01-v2/`.

Среда: Linux 7.2.6-arch2-1, x86_64; AMD Ryzen 9 9950X (16 ядер / 32 потока); Python 3.14.7, NumPy 2.5.3, SciPy 1.18.1. Использовался offline Python/NumPy/SciPy CPU path. Каждый carrier/view/mode исполнен один раз, без прогрева и повторов. Время зависит от нагрузки/частот; GLES/GPU renderer, synchronization/readback, target Aurora device и full frame pipeline не измерялись.

SHA-256 config: `cdf90d578d165f63734678a0709e9bc3a71199e13ba7095771bd351e4d1a9bca`; manifest: `5109162ec2a52b0de176b64bb27a2469f0a77c3eb4ba84f3191114c4ef7ac7a8`; ground truth: `6ab6faa1c9f68e43081a5669c1ded6b3caff450af86531d819a3469861d3e851`.

| Реализация | SHA-256 |
|---|---|
| `tools/run_e_stitch_01.py` | `576f755611f05cf16fecadf9143faf9741cce4348c9f35ff738b90b1fba5550d` |
| `tools/fusion.py` | `8b547e30d7da630063afbc4034b23e924341b97bfbd318c38c5aa9186a6498b9` |
| `tools/stitch_metrics.py` | `de8d287f1f7b098d526f8677f5e96c59a77bf6f48dd7bde7c36ef250bcef1814` |
| `tools/reference.py` | `68f9bdc75bb72b615a0def04d85ed3c2beabeacecc54bc9a667c05a917384840` |

## Медианы по 12 carrier/view сочетаниям для каждого fusion mode

| Fusion mode | Seam pixels | p95 CIE76 output step | L-gradient jump | Ghost source proxy | Python CPU fusion ms |
|---|---:|---:|---:|---:|---:|
| `angular_feather` | 1041 | 1.601 | 0.2329 | 1.7852% | 58.42 |
| `edge_feather` | 1329 | 1.022 | 0.2493 | 1.6265% | 42.65 |
| `graph_cut_multi_band` | 1036 | 2.339 | 0.2766 | 1.8503% | 543.41 |
| `graph_cut_seam` | 1036 | 29.461 | 0.3484 | 1.8381% | 243.15 |
| `hard_best_angle` | 1041 | 20.317 | 0.4090 | 1.8440% | 54.55 |
| `multi_band` | 1329 | 1.019 | 0.2429 | 1.6701% | 317.19 |
| `seam_distance_feather` | 1224 | 1.769 | 0.2398 | 1.5520% | 80.80 |

Каждая строка агрегирует 12 условий из одного capture; это описательная сводка, **не статистически независимые повторы и не рейтинг**. Поддержка seam score отличается по режимам, поэтому в отчёте каждой строки указано число измеренных seam pixels. Более высокий seam proxy у `graph_cut_seam` не доказывает худшее качество: метрика считает цветовой шаг на границе весовых labels и не отделяет scene-edge от шва, а в 3–4 camera overlap бинарный cut не используется. Медиана offline fusion time у graph-cut на этом хосте около 243 ms, что сигнализирует о стоимости текущего reference path, но не предсказывает GPU/runtime time.

## Пределы вывода и следующие проверки

- Seam score — CIE76 между соседними пикселями итогового изображения через argmax-weight label boundary плюс luminance-gradient jump. Нужны независимые scene/object annotations, чтобы отделить реальный объектный край от артефакта шва.
- Ghost score — source-disagreement proxy, требующий обе разнесённые source edges и fused responses; он не считает semantic object IDs и не заменяет оценку по ground truth.
- `graph_cut_seam` сейчас решает independent binary s-t cuts в overlap областях ровно двух камер. В областях трёх/четырёх камер применяется centrality fallback; global four-label/alpha-expansion отсутствует.
- Временной эксперимент не применяет вычисленную позу автомобиля. Один static capture не позволяет подтвердить temporal seam stability.
- RGB Scene Truth oracle не совпадает с геометрией/разметкой этого Blender street capture. Не использовать PSNR/SSIM относительно неё как pixel truth.
- Неравные visibility ROI и одна сцена не позволяют ранжировать carriers. Нужны независимые scenes/pose seeds, object-ID/edge truth, динамические clips и сопоставимые mesh/memory budgets.

Подробная таблица всех 84 случаев следует ниже. Исторические v1 результаты и ранее неверные значения сохранены отдельно в [[validation/STITCH_VISIBILITY]]. Актуальные задачи: [[../TODO]], исследовательский протокол: [[research/PROJECTION_AND_STITCHING]].

---

# Exploratory matrix E-STITCH-01: варианты сшивки и геометрии

Одна статическая capture × 6 carriers × 2 virtual views × 7 fusion variants (84 cases).
Это exploratory screen, а не независимая подтверждающая серия; для выводов нужны holdout scenes/seeds.
`fusion_ms` измеряет только последовательные Python/NumPy/SciPy операции на CPU, не GPU pipeline.
Ghost fraction — source-disagreement proxy, зависящий от fused output; он ещё должен быть сопоставлен с object-ID truth.

| Носитель | Ракурс | Стратегия Fusion | Seam pixels | Seam CIE76 p95 | L-gradient jump | Fused ghost proxy % | Exact Depth Cov % | Consistent Weight % | Python CPU ms |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `plane` | `oblique` | `hard_best_angle` | 1041 | 15.24 | 0.25 | 1.40% | 25.63% | 22.50% | 54.31 |
| `plane` | `oblique` | `edge_feather` | 1329 | 0.81 | 0.18 | 1.48% | 25.63% | 21.76% | 41.77 |
| `plane` | `oblique` | `angular_feather` | 1041 | 1.20 | 0.18 | 1.47% | 25.63% | 22.32% | 56.98 |
| `plane` | `oblique` | `seam_distance_feather` | 1220 | 1.75 | 0.18 | 1.37% | 25.63% | 22.10% | 80.50 |
| `plane` | `oblique` | `graph_cut_seam` | 1041 | 29.49 | 0.29 | 1.49% | 25.63% | 22.44% | 309.57 |
| `plane` | `oblique` | `multi_band` | 1329 | 0.82 | 0.18 | 1.51% | 25.63% | 21.76% | 330.54 |
| `plane` | `oblique` | `graph_cut_multi_band` | 1041 | 2.33 | 0.24 | 1.50% | 25.63% | 22.44% | 605.27 |
| `plane` | `low` | `hard_best_angle` | 1031 | 26.27 | 0.56 | 2.30% | 24.50% | 21.34% | 54.59 |
| `plane` | `low` | `edge_feather` | 1182 | 1.23 | 0.38 | 2.24% | 24.50% | 19.99% | 41.92 |
| `plane` | `low` | `angular_feather` | 1031 | 2.03 | 0.30 | 2.24% | 24.50% | 20.95% | 59.62 |
| `plane` | `low` | `seam_distance_feather` | 1143 | 1.79 | 0.36 | 2.02% | 24.50% | 20.69% | 81.95 |
| `plane` | `low` | `graph_cut_seam` | 938 | 11.94 | 0.48 | 2.15% | 24.50% | 21.29% | 293.26 |
| `plane` | `low` | `multi_band` | 1182 | 1.28 | 0.38 | 2.42% | 24.50% | 19.99% | 324.20 |
| `plane` | `low` | `graph_cut_multi_band` | 938 | 2.38 | 0.34 | 2.16% | 24.50% | 21.29% | 606.00 |
| `bowl` | `oblique` | `hard_best_angle` | 1039 | 13.26 | 0.29 | 1.65% | 24.11% | 21.21% | 56.99 |
| `bowl` | `oblique` | `edge_feather` | 1309 | 0.82 | 0.19 | 1.64% | 24.11% | 20.77% | 42.54 |
| `bowl` | `oblique` | `angular_feather` | 1039 | 1.20 | 0.18 | 1.66% | 24.11% | 21.11% | 59.13 |
| `bowl` | `oblique` | `seam_distance_feather` | 1048 | 1.77 | 0.22 | 1.55% | 24.11% | 20.99% | 83.49 |
| `bowl` | `oblique` | `graph_cut_seam` | 1029 | 33.04 | 0.28 | 1.73% | 24.11% | 21.13% | 250.11 |
| `bowl` | `oblique` | `multi_band` | 1309 | 0.82 | 0.19 | 1.68% | 24.11% | 20.77% | 314.91 |
| `bowl` | `oblique` | `graph_cut_multi_band` | 1029 | 2.35 | 0.21 | 1.73% | 24.11% | 21.13% | 544.10 |
| `bowl` | `low` | `hard_best_angle` | 1078 | 27.12 | 0.55 | 2.19% | 22.14% | 19.10% | 53.53 |
| `bowl` | `low` | `edge_feather` | 1245 | 1.25 | 0.41 | 2.15% | 22.14% | 18.06% | 42.78 |
| `bowl` | `low` | `angular_feather` | 1078 | 2.01 | 0.29 | 2.10% | 22.14% | 18.81% | 59.15 |
| `bowl` | `low` | `seam_distance_feather` | 1228 | 1.89 | 0.39 | 1.77% | 22.14% | 18.63% | 80.86 |
| `bowl` | `low` | `graph_cut_seam` | 1031 | 10.79 | 0.43 | 2.09% | 22.14% | 19.10% | 294.07 |
| `bowl` | `low` | `multi_band` | 1245 | 1.22 | 0.43 | 2.24% | 22.14% | 18.06% | 317.08 |
| `bowl` | `low` | `graph_cut_multi_band` | 1031 | 2.18 | 0.33 | 2.14% | 22.14% | 19.10% | 590.06 |
| `dome_floor` | `oblique` | `hard_best_angle` | 1041 | 15.24 | 0.25 | 1.38% | 25.08% | 22.03% | 54.54 |
| `dome_floor` | `oblique` | `edge_feather` | 1329 | 0.81 | 0.18 | 1.40% | 25.08% | 21.30% | 42.90 |
| `dome_floor` | `oblique` | `angular_feather` | 1041 | 1.20 | 0.18 | 1.44% | 25.08% | 21.85% | 56.85 |
| `dome_floor` | `oblique` | `seam_distance_feather` | 1026 | 1.77 | 0.18 | 1.32% | 25.08% | 21.66% | 79.09 |
| `dome_floor` | `oblique` | `graph_cut_seam` | 1024 | 30.04 | 0.28 | 1.46% | 25.08% | 21.97% | 232.51 |
| `dome_floor` | `oblique` | `multi_band` | 1329 | 0.82 | 0.18 | 1.43% | 25.08% | 21.30% | 330.94 |
| `dome_floor` | `oblique` | `graph_cut_multi_band` | 1024 | 2.34 | 0.23 | 1.47% | 25.08% | 21.97% | 526.74 |
| `dome_floor` | `low` | `hard_best_angle` | 1191 | 25.84 | 0.53 | 2.41% | 16.33% | 14.18% | 54.17 |
| `dome_floor` | `low` | `edge_feather` | 1592 | 1.23 | 0.30 | 1.93% | 16.33% | 13.31% | 42.84 |
| `dome_floor` | `low` | `angular_feather` | 1191 | 2.01 | 0.28 | 2.25% | 16.33% | 13.92% | 58.52 |
| `dome_floor` | `low` | `seam_distance_feather` | 1251 | 1.95 | 0.24 | 1.86% | 16.33% | 13.79% | 82.59 |
| `dome_floor` | `low` | `graph_cut_seam` | 1244 | 14.25 | 0.44 | 2.36% | 16.33% | 14.20% | 236.20 |
| `dome_floor` | `low` | `multi_band` | 1592 | 1.25 | 0.27 | 2.02% | 16.33% | 13.31% | 332.32 |
| `dome_floor` | `low` | `graph_cut_multi_band` | 1244 | 2.40 | 0.32 | 2.38% | 16.33% | 14.20% | 542.73 |
| `cylinder_floor` | `oblique` | `hard_best_angle` | 1041 | 15.24 | 0.25 | 1.38% | 25.08% | 22.03% | 54.32 |
| `cylinder_floor` | `oblique` | `edge_feather` | 1329 | 0.81 | 0.18 | 1.40% | 25.08% | 21.30% | 44.39 |
| `cylinder_floor` | `oblique` | `angular_feather` | 1041 | 1.20 | 0.18 | 1.44% | 25.08% | 21.85% | 58.84 |
| `cylinder_floor` | `oblique` | `seam_distance_feather` | 1026 | 1.77 | 0.18 | 1.32% | 25.08% | 21.66% | 80.79 |
| `cylinder_floor` | `oblique` | `graph_cut_seam` | 1024 | 30.04 | 0.28 | 1.46% | 25.08% | 21.97% | 232.04 |
| `cylinder_floor` | `oblique` | `multi_band` | 1329 | 0.82 | 0.18 | 1.43% | 25.08% | 21.30% | 320.17 |
| `cylinder_floor` | `oblique` | `graph_cut_multi_band` | 1024 | 2.34 | 0.23 | 1.47% | 25.08% | 21.97% | 528.57 |
| `cylinder_floor` | `low` | `hard_best_angle` | 1189 | 25.76 | 0.53 | 2.13% | 16.40% | 14.27% | 54.55 |
| `cylinder_floor` | `low` | `edge_feather` | 1626 | 1.23 | 0.27 | 1.77% | 16.40% | 13.39% | 41.99 |
| `cylinder_floor` | `low` | `angular_feather` | 1189 | 2.01 | 0.29 | 2.06% | 16.40% | 14.00% | 57.69 |
| `cylinder_floor` | `low` | `seam_distance_feather` | 1251 | 1.88 | 0.24 | 1.71% | 16.40% | 13.88% | 80.02 |
| `cylinder_floor` | `low` | `graph_cut_seam` | 1244 | 17.38 | 0.44 | 2.07% | 16.40% | 14.29% | 227.13 |
| `cylinder_floor` | `low` | `multi_band` | 1626 | 1.23 | 0.26 | 1.85% | 16.40% | 13.39% | 316.64 |
| `cylinder_floor` | `low` | `graph_cut_multi_band` | 1244 | 2.41 | 0.31 | 2.10% | 16.40% | 14.29% | 522.78 |
| `cube_floor` | `oblique` | `hard_best_angle` | 1041 | 15.24 | 0.25 | 1.38% | 25.08% | 22.03% | 55.33 |
| `cube_floor` | `oblique` | `edge_feather` | 1329 | 0.81 | 0.18 | 1.40% | 25.08% | 21.30% | 42.27 |
| `cube_floor` | `oblique` | `angular_feather` | 1041 | 1.20 | 0.18 | 1.45% | 25.08% | 21.85% | 58.90 |
| `cube_floor` | `oblique` | `seam_distance_feather` | 1026 | 1.77 | 0.18 | 1.32% | 25.08% | 21.66% | 80.04 |
| `cube_floor` | `oblique` | `graph_cut_seam` | 1024 | 30.04 | 0.28 | 1.46% | 25.08% | 21.97% | 230.86 |
| `cube_floor` | `oblique` | `multi_band` | 1329 | 0.82 | 0.18 | 1.43% | 25.08% | 21.30% | 317.31 |
| `cube_floor` | `oblique` | `graph_cut_multi_band` | 1024 | 2.34 | 0.23 | 1.47% | 25.08% | 21.97% | 532.80 |
| `cube_floor` | `low` | `hard_best_angle` | 1189 | 27.85 | 0.54 | 2.04% | 16.44% | 14.31% | 54.91 |
| `cube_floor` | `low` | `edge_feather` | 1563 | 1.23 | 0.26 | 1.57% | 16.44% | 13.40% | 43.64 |
| `cube_floor` | `low` | `angular_feather` | 1189 | 2.00 | 0.28 | 1.91% | 16.44% | 14.04% | 57.77 |
| `cube_floor` | `low` | `seam_distance_feather` | 1247 | 1.66 | 0.24 | 1.56% | 16.44% | 13.89% | 79.81 |
| `cube_floor` | `low` | `graph_cut_seam` | 1240 | 31.77 | 0.41 | 1.95% | 16.44% | 14.30% | 230.45 |
| `cube_floor` | `low` | `multi_band` | 1563 | 1.25 | 0.26 | 1.66% | 16.44% | 13.40% | 316.92 |
| `cube_floor` | `low` | `graph_cut_multi_band` | 1240 | 2.50 | 0.31 | 1.97% | 16.44% | 14.30% | 528.03 |
| `burger_like` | `oblique` | `hard_best_angle` | 1041 | 15.24 | 0.25 | 1.45% | 26.18% | 22.99% | 53.96 |
| `burger_like` | `oblique` | `edge_feather` | 1255 | 0.81 | 0.23 | 1.61% | 26.18% | 22.28% | 41.66 |
| `burger_like` | `oblique` | `angular_feather` | 1041 | 1.20 | 0.18 | 1.52% | 26.18% | 22.81% | 57.19 |
| `burger_like` | `oblique` | `seam_distance_feather` | 1352 | 1.58 | 0.28 | 1.45% | 26.18% | 22.63% | 83.05 |
| `burger_like` | `oblique` | `graph_cut_seam` | 1045 | 29.44 | 0.29 | 1.54% | 26.18% | 22.92% | 464.33 |
| `burger_like` | `oblique` | `multi_band` | 1255 | 0.82 | 0.23 | 1.65% | 26.18% | 22.28% | 315.37 |
| `burger_like` | `oblique` | `graph_cut_multi_band` | 1045 | 2.33 | 0.25 | 1.55% | 26.18% | 22.92% | 762.62 |
| `burger_like` | `low` | `hard_best_angle` | 1180 | 25.40 | 0.53 | 2.19% | 16.61% | 14.47% | 54.57 |
| `burger_like` | `low` | `edge_feather` | 1542 | 1.23 | 0.45 | 1.94% | 16.61% | 13.59% | 42.76 |
| `burger_like` | `low` | `angular_feather` | 1180 | 2.01 | 0.29 | 2.08% | 16.61% | 14.20% | 58.32 |
| `burger_like` | `low` | `seam_distance_feather` | 1593 | 1.66 | 0.47 | 1.65% | 16.61% | 14.05% | 80.81 |
| `burger_like` | `low` | `graph_cut_seam` | 1090 | 11.01 | 0.50 | 2.17% | 16.61% | 14.45% | 511.58 |
| `burger_like` | `low` | `multi_band` | 1542 | 1.25 | 0.46 | 2.00% | 16.61% | 13.59% | 315.76 |
| `burger_like` | `low` | `graph_cut_multi_band` | 1090 | 2.13 | 0.34 | 2.21% | 16.61% | 14.45% | 814.74 |

## Границы вывода

Варианты fusion здесь исполняются на CPU в offline reference, а не в GLES shader. Seam CIE76 — шаг между соседними output pixels на границе argmax-weight labels. Ghost proxy показывает только сохранение обоих разнесённых source contours в fused image; он не заменяет независимую semantic/object truth.

Эта матрица не является независимым повтором: все 84 случая используют один capture. Нельзя по ней объявлять лучший carrier/fusion или target-device frame rate.
